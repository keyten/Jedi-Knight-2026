# Jedi Academy 2026 rendering features

This page is a practical reference for rendering features added by Jedi Academy 2026. **Requirements** mention existing Rend2 settings or other features that must already be enabled. Command lists contain only controls introduced by the feature itself. Comparison modes and diagnostics are listed separately under **Debug**.

# Colors & post-processing

## Tone mapping

Changes how the HDR scene is compressed into the display range. The difference is easiest to see in bright skies, lamps, sabers, explosions and dark-to-bright transitions. ACES gives stronger cinematic contrast and highlight rolloff; AgX-like is more neutral while keeping bright detail.

**Requirements.** Works only with HDR enabled (`r_hdr 1`) and tone mapping enabled. Auto exposure is recommended with HDR (`r_autoExposure 1`); exposure can also be adjusted with the existing `r_exposureCompensation` / `r_cameraExposure` controls.

**Main controls**
- `r_toneMapMode 0 | 1 | 2` — 0 legacy Rend2 filmic, 1 ACES fitted, 2 AgX-like.

**Debug**
- `r_toneMapDebug 1` — legacy vs selected tone mapper split.
- `r_toneMapDebug 2` — legacy / ACES / AgX comparison.
- `r_toneMapDebug 3..5` — raw HDR, exposure-adjusted and exposure false-color views.

No measurable effect on frame performance.

## Color grading LUTs

Applies a 3D color lookup table after rendering, allowing different maps to have different palettes, contrast and mood. A map can automatically use `maps/<map>.cube`, while a user can override it with another `.cube` LUT.

**Requirements.** No special rendering mode is required. LUT files use the common `.cube` format.

**Main controls**
- `r_colorGrading 0 | 1` — disable / enable color grading.
- `r_colorGradingLUT "<file>.cube"` — select a LUT manually; empty uses `maps/<map>.cube` if present; `*identity` is neutral.
- `r_colorGradingIntensity 0..1` — blend between the original image and the LUT.

**Debug**
- `r_colorGradingCompare 0 | 1` — disable / enable split-screen comparison, original on the left.

May slightly affect performance.

## Linear lighting

Calculates lighting in linear light instead of directly in display-encoded sRGB. It mainly changes how lightmaps, dark gradients and bright lighting combine, and makes the pipeline more physically consistent with HDR.

**Requirements.** Requires HDR (`r_hdr 1`) and tone mapping (`r_toneMap 1`).

**Main controls**
- `r_linearLighting 0 | 1` — legacy / linear-light handling of non-HDR lightmaps. Requires `vid_restart`.

No measurable effect on frame performance.

## HDR bloom

Adds soft glow around genuinely bright HDR sources such as sabers, emissive panels, explosions and intense lights. Unlike the legacy LDR glow path, it works from scene-linear brightness before tone mapping.

**Requirements.** Requires HDR (`r_hdr 1`) and tone mapping (`r_toneMap` or `r_forceToneMap`).

**Main controls**
- `r_bloom -1 | 0 | 1` — legacy glow / off / HDR bloom.

**Other controls**
- `r_bloomIntensity <value>` — bloom strength.
- `r_bloomThreshold <value>` — scene-linear brightness where optional scene bloom begins.
- `r_bloomKnee <value>` — softness around the threshold.
- `r_bloomScatter <value>` — how widely the bloom pyramid spreads.
- `r_bloomSceneIntensity <value>` — contribution from bright non-emissive scene pixels; `0` keeps bloom emissive-only.

May slightly affect performance.

## Velocity motion blur

Adds camera and object motion streaks while keeping HUD and menus sharp. It uses per-pixel motion rather than simply blurring the whole frame, so rotating, running and moving characters produce different vectors.

**Requirements.** Requires HDR (`r_hdr 1`).

**Main controls**
- `r_motionBlur 0 | 1` — disable / enable velocity motion blur. Requires `vid_restart`.

**Other controls**
- `r_motionBlurShutterAngle <degrees>` — exposure length.
- `r_motionBlurReferenceFps <fps>` — FPS the shutter angle refers to; `0` uses the current frame interval.
- `r_motionBlurMaxPixels <pixels>` — maximum streak length at 1080p.
- `r_motionBlurQuality 0 | 1 | 2` — low / medium / high.
- `r_motionBlurSamples <count>` — sample override; `0` uses the quality preset.
- `r_motionBlurViewModelScale <value>` — blur strength for first-person hands, weapons and saber.
- `r_motionBlurCutDistance <units>` — one-frame translation treated as a camera cut.
- `r_motionBlurCutAngle <degrees>` — one-frame rotation treated as a camera cut.
- `r_motionBlurShutterScale <value>` — gameplay exposure multiplier, used by effects such as Force Speed.
- `r_motionBlurReset 1` — gameplay hook that clears motion history after a cut.

**Debug**
- `r_motionBlurDebug 1..5` — velocity, camera/object velocity, sample count and blur contribution.

Moderate GPU cost, increasing with quality and screen resolution.

# Materials & surface detail

## Auto PBR material classification

Makes legacy materials without authored PBR maps react more plausibly to lights and reflections. Jedi Academy 2026 can either apply one generic dielectric fallback or classify common materials such as metal, cloth, leather, skin and plastic from asset names.

**Requirements.** Applies only to legacy lit materials without authored specular/ORM data. Authored material data always wins.

**Main controls**
- `r_autoPBR 0 | 1 | 2` — legacy fallback / generic dielectric / heuristic material classes.

**Debug**
- `r_autoPBRDebug 1` — material classes.
- `r_autoPBRDebug 2` — source of the PBR parameters.
- `r_pbrDumpMaterials [used|all|auto|authored|gouraud|<class>]` — list registered material sources and values.

No meaningful frame-time cost; classification happens during material registration.

## Automatic roughness variation

Adds subtle spatial roughness variation to diffuse-only materials instead of giving every pixel one constant class roughness. It derives small-scale variation from local diffuse detail while avoiding brightness-based “fake metal” or AO generation.

**Requirements.** Requires `r_autoPBR 1` or `2`. Only legacy diffuse-only materials are modified; authored specular/ORM maps, explicit scalar material values and converted legacy specular shaders are left untouched.

**Main controls**
- `r_autoPBRRoughness 0 | 1` — constant class roughness / generate an automatic roughness map. Turning it on requires `vid_restart`.

**Debug**
- `r_autoPBRDebug 3` — final generated roughness in grey; blue means the material is not using auto roughness.
- `r_pbrDumpMaterials` — also reports generated roughness maps and generation time.

The generated maps add some loading work and texture memory, but little additional frame-time cost.

## Legacy shiny material conversion

Converts old `alphaGen lightingSpecular` / fake `tcGen environment` materials from the vertex-lit path to the per-pixel PBR path. Their existing specular mask is reused as spatial PBR information, reducing characteristic Gouraud-style highlights on old shiny assets.

**Requirements.** Intended for legacy shaders using those old shine techniques.

**Main controls**
- `r_autoPBRConvert 0 | 1` — keep the old vertex-lit path / convert compatible shaders to per-pixel PBR. Requires `vid_restart`.

May slightly increase GPU cost on converted surfaces because they move to per-pixel lighting.

## Diffuse BRDF

Changes the diffuse reflection model of standard PBR materials. Burley/Disney accounts for roughness and viewing/light angles while Lambert is the simpler legacy-style diffuse response.

**Requirements.** Affects standard PBR diffuse lighting; cloth uses its own BRDF path.

**Main controls**
- `r_diffuseBRDF 0 | 1` — Lambert / Burley-Disney diffuse.

No measurable effect on frame performance.

## Emissive materials

**For mod authors.** Materials can now emit linear HDR radiance independently from reflected light. This is useful for lamps, screens, control panels and other surfaces that should feed HDR bloom and SSGI without making an entire diffuse stage glow.

**Requirements.** No global enable cvar is required. HDR is recommended for intensities above 1.

~~~text
{
    map textures/example/panel_d
    emissiveMap textures/example/panel_e
    emissiveColor 1.0 0.25 0.05
    emissiveScale 4.0
}
~~~

- `emissiveMap <image>` — emission texture; black texels do not emit.
- `emissiveColor <r g b>` — linear emission color.
- `emissiveScale <value>` — scalar HDR intensity.
- `emissiveScale <r g b>` — RGB intensity shorthand.

No meaningful cost on materials that do not use the keywords; emissive materials add only a small amount of shading work.

## Automatic emissive compatibility

Lets existing stock-style glow and safe unlit additive stages act as physical emission sources without editing their shader files. This is useful for SSGI and HDR bloom on existing maps.

**Requirements.** Works on compatible legacy `glow` stages and standalone unlit additive stages. The renderer deliberately rejects ambiguous lit/additive stages to avoid double lighting.

**Main controls**
- `r_autoEmissive 0 | 1` — legacy behavior / expose compatible legacy glow stages as emissive sources.

No meaningful effect on performance.

## Adaptive POM traversal

Improves ordinary Parallax Occlusion Mapping by using fewer samples when looking straight at a surface and more at grazing angles. It can also fade POM into normal mapping with distance.

**Requirements.** Requires ordinary POM (`r_parallaxMapping 1`), normal mapping and a height source such as `normalHeightMap`, a discovered `_nh` texture or `heightMap`.

**Main controls**
- `r_pomAdaptiveSteps 0 | 1` — legacy fixed traversal / angle-adaptive traversal.

**Other controls**
- `r_pomMinSteps <count>` — linear samples at normal incidence.
- `r_pomMaxSteps <count>` — linear samples at grazing angles.
- `r_pomBinarySteps <count>` — final hit refinement.
- `r_pomFadeStart <distance>` — distance where relief begins flattening.
- `r_pomFadeEnd <distance>` — distance where POM becomes normal mapping only.

This can reduce POM cost at easy angles and long distances, but grazing views may use more samples.

## POM self-shadowing

Adds shadows inside POM relief. Brick grooves, cracks and raised panels can shadow themselves under the sun and nearby dynamic lights instead of looking like unshadowed texture displacement.

**Requirements.** Requires `r_parallaxMapping 1`, normal mapping and height data. Sun self-shadowing also needs realtime sunlight (`r_sunlightMode 1` or `2`).

**Main controls**
- `r_pomSelfShadow 0 | 1` — disable / enable relief self-shadowing. Requires `vid_restart`.

**Other controls**
- `r_pomSelfShadowLightMode 0..3` — sun only / sun + strongest local / sun + limited local lights / all local lights.
- `r_pomSelfShadowMaxLocalLights <count>` — local-light budget for mode 2.
- `r_pomSelfShadowSteps <count>` — samples per shadow ray.
- `r_pomSelfShadowStrength <value>` — global shadow strength.
- `r_pomSelfShadowBias <value>` — ray-start bias inside the height field.
- `r_pomSelfShadowSoftness <value>` — penetration-to-shadow scale.

**Debug**
- `r_pomDebug 1..8` — height, displaced UV, hit depth, sun/local self-shadow, sample counts and distance fade. Requires `vid_restart` when enabling the debug permutation.
- `r_pomDebugFreezeLight 0 | 1` — freeze the sun direction for comparison.

**Mod author override.** A material can use `pomSelfShadow <0..1>` and an explicit `heightMap`:

~~~text
{
    map textures/test/bricks
    normalMap textures/test/bricks_n
    heightMap textures/test/bricks_h
    parallaxDepth 0.06
    pomSelfShadow 0.8
}
~~~

Can significantly affect performance on POM-heavy scenes, especially with several self-shadowed local lights.

## Silhouette POM

**For mod authors.** Ordinary POM cannot change the edge of a triangle. Silhouette POM builds a small shell around selected surfaces so deep relief can carve or extend the actual visible silhouette and write displaced depth.

**Requirements.** Requires `r_pomSilhouette 1`, `r_parallaxMapping 1`, normal mapping, a height field, and a compatible opaque surface. It is best for rocks, broken stone, chunky panels and strong edge relief rather than ordinary floors.

~~~text
textures/test/rock_edge
{
    pomSilhouette
    pomSilhouetteDistance 384
    pomSilhouetteSteps 32
    {
        map textures/test/rock_edge
        normalHeightMap textures/test/rock_edge_nh
        parallaxDepth 0.06
        parallaxBias 0.5
        rgbGen identity
    }
}
~~~

**Main controls**
- `r_pomSilhouette 0 | 1` — disable / enable shell rendering. Requires `vid_restart`.

**Other controls**
- `r_pomSilhouetteDistance <units>` — shell range; farther surfaces use ordinary POM.
- `r_pomSilhouetteFade <units>` — crossfade width.
- `r_pomSilhouetteSteps <count>` — normal-angle ray samples.
- `r_pomSilhouetteMaxSteps <count>` — grazing-angle ray samples.
- `r_pomSilhouetteBinarySteps <count>` — hit refinement.
- `r_pomSilhouetteViewDependence <value>` — how quickly samples increase toward grazing angles.
- `r_pomSilhouetteShadows 0 | 1` — displaced shells cast into sun cascades.
- `r_pomSilhouetteContactShadows 0 | 1` — apply screen-space sun contact shadows to shell pixels.

**Debug**
- `r_pomSilhouetteDebug 1..11` — shell, wireframe, walls, discarded pixels, virtual depth, sample count and comparison views.
- `r_pomSilhouetteInfo` — print shell counts, memory, fallbacks and last-frame statistics.

Can significantly affect performance because every visible shell pixel ray-traces the height field and writes displaced depth.

## Automatic silhouette POM

Automatically enables silhouette POM for compatible materials that already have ordinary POM height data, so stock `_nh` assets can be tested without editing shader files.

**Requirements.** `r_pomSilhouette 1` must already be active when the map is loaded. Materials still need ordinary POM-compatible height data and eligible geometry.

**Main controls**
- `r_autoPOMSilhouette 0 | 1` — disable / enable automatic mode globally.
- `r_autoPOMSilhouette <shader> 0 | 1 | default` — per-shader override; a trailing `*` acts as a prefix.
- `r_autoPOMSilhouette clear` — remove per-shader overrides.

**Debug / inspection**
- `r_autoPOMSilhouette list` — list active candidates and fallback reasons.
- `r_autoPOMSilhouette <shader>` — print matching shader state.

Per-shader switches are live, but shell data is prepared at map load. Performance is the same as explicit silhouette POM for surfaces that are actually enabled.

# Lighting & shadows

## GTAO

Adds stronger and more stable ambient occlusion in corners, wall/floor contacts, around props and under nearby geometry. The newer path also stores a bent normal so indirect diffuse and environment specular can prefer directions that are actually open.

**Requirements.** Requires the depth prepass (`r_depthPrepass 1`). Starting the renderer with AO completely disabled and enabling it later requires `vid_restart` so the required buffers exist.

**Main controls**
- `r_aoMode -1 | 0 | 1 | 2` — follow legacy `r_ssao` / off / legacy SSAO / GTAO.

**Other controls**
- `r_aoApply -1 | 0 | 1` — automatic / legacy application / indirect-light-only application.
- `r_aoMultiBounce 0 | 1` — preserve more indirect light on bright surfaces.
- `r_aoLightmapFraction <value>` — share of baked light treated as indirect.
- `r_aoSpecOcclusion -1 | 0 | 1 | 2` — auto / scalar / Lagarde / bent-normal cone specular occlusion.
- `r_gtaoQuality 0..3` — low / medium / high / ultra.
- `r_gtaoHalfRes 0 | 1` — full / half resolution. Requires `vid_restart`.
- `r_gtaoRadius <units>` — world-space radius.
- `r_gtaoFalloff <value>` — distance falloff.
- `r_gtaoThickness <value>` — thin-occluder compensation.
- `r_gtaoPower <value>` — visibility contrast.
- `r_gtaoDenoise 0..3` — edge-aware denoise passes.
- `r_gtaoBentNormals 0..1` — bent-normal strength for indirect lighting.

**Debug**
- `r_aoCompare 0 | 1` — split-screen legacy SSAO vs GTAO.
- `r_aoDebug 1..10` — AO, depth, normal, contact and bent-normal views.

Moderate GPU cost; half-resolution GTAO is substantially cheaper than full resolution.

## Screen-space contact shadows

Adds short sun shadows that cascaded shadow maps often miss, such as feet touching the floor, thin edges and small gaps.

**Requirements.** Requires realtime sunlight and the depth prepass. Enabling the feature after starting without its screen-space buffers requires `vid_restart`.

**Main controls**
- `r_contactShadows 0 | 1` — disable / enable sun contact shadows.

**Other controls**
- `r_contactShadowLength <units>` — maximum ray length.
- `r_contactShadowSteps <count>` — ray samples.
- `r_contactShadowThickness <units>` — assumed blocker thickness.
- `r_contactShadowStrength <value>` — shadow strength.
- `r_contactShadowSoft 0 | 1` — first-hit / softer depth-weighted result.

Moderate GPU cost, mostly proportional to screen coverage and ray steps.

## Forward+ dynamic lighting

Replaces per-surface 32-light masks with clustered light lists, allowing many sabers, blasters and explosions to coexist without dropping lights. The view is divided into screen tiles and logarithmic depth slices, and each cluster keeps only relevant lights.

**Requirements.** No other new feature is required. Features such as LTC area lights use Forward+ as a dependency.

**Main controls**
- `r_forwardPlus 0 | 1` — legacy dynamic-light masks / clustered Forward+.

**Other controls**
- `r_forwardPlusTileSize <pixels>` — screen tile size.
- `r_forwardPlusSlices <count>` — logarithmic depth slices.
- `r_forwardPlusNearSlice <units>` — depth covered by the first slice.
- `r_forwardPlusMaxLightsPerCluster <count>` — light cap per cluster.
- `r_forwardPlusMaxShadowLights <count>` — strongest dynamic lights allowed shadow cubes when `r_dlightMode 2` is active.

**Debug**
- `r_forwardPlusDebug 1..9` — tiles, slices, clusters, light counts, overflow, shadows and selected light. Requires `vid_restart`.
- `r_forwardPlusDebugLight <index>` — selected light for debug mode 9.
- `r_forwardPlusStats` — print cluster statistics.
- `r_forwardPlusBenchmark [frames]` — benchmark the lighting path.
- `r_forwardPlusSpawnTestLights <count> [spread]` — spawn test lights.

Usually inexpensive and can improve many-light scenes; very dense light clusters still increase shading cost.

## Screen-space global illumination

Bounces nearby **dynamic diffuse light and physical emission** through visible surfaces. A saber can light a wall directly and that wall can then cast a weaker colored bounce onto the floor. By default it does not bounce the already-baked map lighting a second time.

**Requirements.** Does not require Forward+ or SSR. A depth prepass is strongly recommended for correct temporal reprojection of moving geometry.

**Main controls**
- `r_ssgi 0 | 1` — disable / enable SSGI. Requires `vid_restart`.
- `r_ssgiSource 0 | 1 | 2` — dynamic + emissive / emissive only / full scene, with mode 2 experimental.

**Other controls**
- `r_ssgiIntensity <value>` — indirect-light strength.
- `r_ssgiQuality 0..3` — low / medium / high / ultra.
- `r_ssgiRays <count>` — rays per traced pixel; `0` uses the quality preset.
- `r_ssgiSteps <count>` — ray-march steps; `0` uses the preset.
- `r_ssgiMaxDistance <units>` — bounce distance.
- `r_ssgiThickness <units>` — assumed depth-buffer surface thickness.
- `r_ssgiTemporal 0 | 1` — temporal accumulation.
- `r_ssgiHistoryWeight <value>` — history weight.
- `r_ssgiDenoise -1..4` — denoise passes; `-1` uses the preset.
- `r_ssgiHalfRes -1 | 0 | 1` — preset / full / half-resolution tracing; changing trace resolution takes effect after `vid_restart`.
- `r_ssgiHiZ -1 | 0 | 1` — preset / linear / hierarchical tracing.
- `r_ssgiEmissiveScale <value>` — physical emissive contribution.
- `r_ssgiGlowScale <value>` — legacy glow/auto-emissive contribution.

**Debug**
- `r_ssgiCompare 0 | 1` — split screen without / with SSGI.
- `r_ssgiDebug 1..10` — hit, distance, raw GI, history, denoise, source and final GI views.
- `r_ssgiFreezeHistory 0 | 1` — freeze temporal history.

Can significantly affect performance, especially at high/ultra quality.

## Diffuse IBL / directional ambient

Makes BSP light-grid ambient less flat by modulating it with directional irradiance derived from runtime cubemap probes. Opposite sides of a character can receive different ambient energy even where no direct light is present.

**Requirements.** Requires usable runtime cubemap probes.

**Main controls**
- `r_diffuseIBL 0 | 1` — disable / enable directional probe ambient. Requires `vid_restart`.
- `r_diffuseIBLStrength <value>` — modulation strength.

**Debug**
- `r_diffuseIBLDebug 1..5` — irradiance, modulation, legacy/new ambient and selected probe.

May slightly affect performance.

## LTC area lights

**For mod authors.** Rectangular and line emitters can produce correctly shaped direct-light highlights instead of behaving like point lights. Static map lamps can add only specular while leaving their baked diffuse lightmap intact.

**Requirements.** Requires `r_forwardPlus 1` and `r_ltcAreaLights 1`.

A map can define `maps/<map>.arealights.json`:

~~~json
{
  "lights": [
    {
      "type": "rect",
      "center": [0, 0, 128],
      "right": [1, 0, 0],
      "up": [0, 1, 0],
      "halfWidth": 64,
      "halfHeight": 8,
      "color": [1.0, 0.85, 0.7],
      "intensity": 4.0,
      "mode": "static_specular"
    }
  ]
}
~~~

Rectangle lights use `center`, `right`, `up`, `halfWidth` and `halfHeight`. Line lights use `start`, `end` and `radius`. `mode` can be `static_specular`, `static_full` or `dynamic`.

**Main controls**
- `r_ltcAreaLights 0 | 1` — disable / enable LTC rectangle and line lights. Requires `vid_restart`.

**Other controls**
- `r_ltcIntensityScale <value>` — global radiance multiplier.
- `r_ltcStaticDiffuse 0 | 1` — also add diffuse for `static_specular` lights.
- `r_ltcMaxLights <count>` — maximum map area lights used per scene.

**Debug**
- `r_ltcDebug 1..8` — specular/diffuse, source mode, cluster counts, bounds, outlines, axes and strongest ID. Requires `vid_restart`.
- `r_ltcDebugLight <id>` — choose the highlighted map light; `-1` chooses the nearest.
- `r_ltcReloadLights` — reload the map file.
- `r_ltcList` — list loaded lights.
- `r_ltcNearest` — print the nearest light.

Moderate GPU cost where area lights affect many pixels.

## Automatic LTC lamp detection

Makes LTC useful on stock maps without authoring an area-light JSON file. At map load the renderer scans glow, emissive and surfacelight surfaces, finds bright connected regions and fits rectangles to them.

**Requirements.** Requires LTC area lights. An authored `maps/<map>.arealights.json` always overrides automatic detection.

**Main controls**
- `r_ltcAutoAreaLights 0 | 1 | 2` — off / confident lamp shapes / also looser candidates.

**Debug / authoring tools**
- `r_ltcReloadLights` — rerun automatic conversion when no authored file exists.
- `r_ltcExtractLights` — write detected candidates to `maps/<map>.arealights.generated.json` for review.

Detection costs time only at map load; per-frame cost is the same as the area lights that were selected.

## Saber line area lights

Makes the actual saber blade a line emitter instead of representing the whole blade with one point light, producing longer and more naturally shaped highlights.

**Requirements.** Requires `r_ltcAreaLights 1` and Forward+.

**Main controls**
- `r_ltcSaberAreaLights 0 | 1` — point-light sabers / LTC line-light sabers.

**Binary note.** This is not renderer-only: saber geometry is submitted by cgame through a new optional renderer API. Full support needs the rebuilt engine and game/cgame modules together with the renderer.

Moderate additional cost around visible sabers.

## Spot lights

**For mod authors.** EFX lights can now be true spot lights instead of point lights. The same cone can light opaque surfaces, cast a projected shadow and become a visible volumetric cone in froxel fog.

**Requirements.** Existing point lights are unchanged. Spot shadows require `r_dlightMode 2`. Volumetric cones additionally require froxel fog (`r_volumetricFog 2`).

~~~text
Light
{
    size { start 700 }
    rgb  { start 1 0.92 0.8 }
    spot
    {
        innerAngle 16
        outerAngle 26
        direction 1 0 0
        shadows 1
    }
}
~~~

**Main controls**
- `r_spotLights 0 | 1` — submit authored spots as ordinary point lights / as spot lights.
- `r_spotLightShadows 0 | 1` — disable / enable spot shadows.

**Debug**
- `r_spotLightDebug 1..4` — cone list, shadow frustums, spot-only surface lighting and spot-only fog lighting.
- `r_spot add|attach|spin|noshadow|list|clear` — renderer-side test spots.
- `fxplay <efx> [distance|muzzle]` — developer test command for authored EFX spots.

**Binary note.** Full EFX support is not renderer-only. SP adds renderer/cgame plumbing for spot lights; MP parses/submits them from the client FX system. Use the updated engine/client and game module together with the renderer.

The cone itself is inexpensive; shadows and volumetric lighting can make spot lights moderately expensive.

## Spot-light cookies / gobos

**For mod authors.** A spot light can project a texture through its cone. The same pattern appears on surfaces and inside volumetric fog, while geometry shadows still multiply with the cookie rather than being replaced by it.

**Requirements.** Requires an authored spot light. Point lights do not have a projection and cannot use cookies.

~~~text
spot
{
    innerAngle 22
    outerAngle 26
    cookie textures/imperial/grate02
    cookieRoll 0
}
~~~

**Main controls**
- `r_spotLightCookies 0 | 1 | 2` — off / intensity-only cookie / colored cookie.

**Debug**
- `r_spotLightCookieDebug 1..3` — projected UV, cookie factor, cookie multiplied by geometry shadow.
- `r_spot cookie <index> <image|none>` — assign a cookie to a renderer-side test spot.

**Binary note.** SP cookie registration uses new engine/cgame plumbing, so authored EFX cookies need the updated engine and game module as well as the renderer.

Adds a texture lookup to affected spot lights and may slightly affect performance.

## Sun Shadows 2.0

Improves cascaded sun shadows with stabilized cascade fitting, blended cascade transitions, better receiver bias and optional PCSS contact-hardening softness.

**Requirements.** Requires the existing realtime sun-shadow path.

**Main controls**
- `r_sunShadowMode 0 | 1` — legacy / Shadows 2.0. Requires `vid_restart`.
- `r_shadowPCSS 0 | 1` — disable / enable PCSS.

**Other controls**
- `r_sunShadowAlphaCasters 0 | 1` — alpha-tested foliage/cutout casters and receivers. Requires `vid_restart`.
- `r_shadowCascadeBlend <value>` — cascade overlap.
- `r_shadowDepthBias <units>` — constant receiver bias.
- `r_shadowNormalBias <value>` — normal offset.
- `r_shadowSlopeBias <value>` — slope-dependent bias.
- `r_shadowReceiverBiasClamp <units>` — maximum receiver correction.
- `r_shadowPCSSQuality 0..2` — low / high / ultra.
- `r_shadowSunAngularDiameter <degrees>` — apparent sun size.
- `r_shadowPCSSMaxPenumbra <units>` — maximum penumbra size.

**Debug**
- `r_shadowDebug 1..11` — cascades, depth, PCSS, contact shadows, bias, point shadows and Ghoul2 receivers.

Moderate GPU cost; high-quality PCSS on large shadowed screen areas can be expensive.

# Player & model lighting

## Entity light-grid lighting

Improves lighting across characters and entities. Instead of sampling one point for the whole model, Jedi Academy 2026 can sample several body positions or sample the BSP light grid per fragment.

**Requirements.** Uses the map's existing BSP light grid.

**Main controls**
- `r_entityLightGrid 0 | 1 | 2` — legacy / three CPU samples / per-fragment GPU samples. Requires `vid_restart`.

**Debug**
- `r_entityLightGridDebug 1..9` — ambient, direct, direction, validity, cell, old/new modes and difference. Requires `vid_restart`.

Mode 1 has little cost; mode 2 has moderate per-pixel cost on lit entities.

## Skin subsurface scattering

Softens diffuse light across skin and optionally adds back-light transmission through thin areas. Mode 1 is a cheap wrapped-diffuse approximation; mode 2 is real screen-space diffusion of the skin diffuse term.

**Requirements.** Mode 2 requires HDR (`r_hdr 1`). Skin is automatically classified, with optional shader overrides for mods.

**Main controls**
- `r_skinSSS 0 | 1 | 2` — off / wrapped diffuse / screen-space diffusion. Requires `vid_restart`.

**Other controls**
- `r_skinSSSMixedHeads 0 | 1` — also process mixed `*_head` textures. Requires `vid_restart`.
- `r_skinSSSStrength 0..1` — share of skin diffuse replaced by the diffused copy.
- `r_skinSSSWidth <mm>` — diffusion radius.
- `r_skinSSSQuality 0..2` — 11 / 17 / 25 taps.
- `r_skinSSSWrap <value>` — mode-1 wrap width.
- `r_skinSSSFollowSurface <value>` — depth/normal edge stopping.
- `r_skinSSSTransmission <value>` — optional back-light term.

**Debug**
- `r_skinSSSCompare 0 | 1` — split screen without / with SSS.
- `r_skinSSSDebug 1..8` — classification, mask, raw diffuse, blur passes, delta and radius.
- `r_skinSSSList` — print skin classification.
- `r_skinSSSKernel` — print the diffusion kernel.

**Mod author overrides.** `skinScatter <0..1>` changes scattering strength and `skinMask <image>` supplies a per-pixel mask.

Mode 1 has a small cost; mode 2 has moderate GPU cost from the two diffusion passes.

## Character shadow improvements

Improves Ghoul2 self-shadowing consistency by controlling caster LOD, using a better dynamic-light cube bias and optionally softening screen-space contact shadows.

**Requirements.** The corresponding sun, dynamic-light or contact-shadow feature must already be active.

**Main controls**
- `r_shadowCasterLod 0 | 1` — shadow-view LOD / camera-view LOD for Ghoul2 shadow casters.
- `r_dlightShadowBias 0 | 1` — legacy / texel-scaled dynamic-light shadow bias.
- `r_contactShadowSoft 0 | 1` — binary / soft contact-shadow sampling.

**Debug**
- `r_shadowCasterStats 0 | 1` — print Ghoul2 caster and LOD statistics.
- `r_shadowDebug 10` — point-light shadow visibility.
- `r_shadowDebug 11` — Ghoul2 receiver/caster view.

Usually a small performance difference.

# Reflections

## Screen-space reflections

Adds real scene reflections to glossy PBR surfaces while retaining cubemaps as the fallback for off-screen, rough or unreliable rays. Sabers and additive effects can also appear as analytic reflected emitters even when the source itself is outside the screen.

**Requirements.** Requires specular mapping (`r_specularMapping 1`). HDR is strongly recommended; the non-HDR scene buffer clamps reflection energy.

**Main controls**
- `r_ssr 0 | 1` — disable / enable hybrid SSR + cubemap reflections. Requires `vid_restart`.

**Other controls**
- `r_ssrQuality 0..3` — low / medium / high / ultra.
- `r_ssrSteps <count>` — ray-march override; `0` uses the preset.
- `r_ssrRefineSteps <count>` — hit-refinement override.
- `r_ssrMaxDistance <units>` — maximum ray length.
- `r_ssrThickness <units>` — assumed depth-buffer thickness.
- `r_ssrMaxRoughness <value>` — roughness where SSR fully fades to cubemap.
- `r_ssrEdgeFade <value>` — screen-edge fade.
- `r_ssrHalfRes -1 | 0 | 1` — preset / full / half-resolution tracing.
- `r_ssrHiZ -1 | 0 | 1` — preset / linear / hierarchical tracing.
- `r_ssrTemporal 0 | 1` — temporal accumulation. Requires `vid_restart`.
- `r_ssrTemporalWeight <value>` — history weight.
- `r_ssrBlendStrength <value>` — confidence/replacement scale.
- `r_ssrHitCache 0 | 1` — reuse still-valid previous-frame ray hits; pixels are periodically retraced.
- `r_ssrReceiverCull 0 | 1` — skip ray marching on pixels that cannot use SSR.
- `r_ssrEmitters 0 | 1` — analytic saber/blaster/effect reflections.
- `r_ssrEmitterIntensity <value>` — analytic emitter brightness.
- `r_ssrEmitterMaxRoughness <value>` — roughness limit for analytic emitters.

**Debug**
- `r_ssrCompare 0 | 1` — cubemap-only / hybrid split screen.
- `r_ssrDebug 1..12` — material, hit, confidence, raw/cubemap/hybrid reflection, emitter and hit-cache views.

Moderate GPU cost at low/medium settings and potentially significant cost at high/ultra. Hit caching and early culling reduce the tracing cost.

# Volumetric lighting & fog

## Froxel volumetric fog

Replaces the old light-grid ray march with a camera-aligned 3D volume. Sunlight, baked light and dynamic lights illuminate fog spatially, shadows affect the medium, and the result is temporally accumulated before being integrated toward the camera.

**Requirements.** Mode 2 requires the depth prepass (`r_depthPrepass 1`); otherwise the renderer falls back to the legacy fog path.

**Main controls**
- `r_volumetricFog 0 | 1 | 2` — off / legacy light-grid ray march / froxel volumetric fog. Requires `vid_restart`.
- `r_volumetricFogQuality 0..2` — low / medium / high. Requires `vid_restart`.

**Other controls**
- `r_volumetricFogGridScale <pixels>` — manual screen pixels per froxel; `0` uses the preset. Requires `vid_restart`.
- `r_volumetricFogSlices <count>` — manual depth slices; `0` uses the preset. Requires `vid_restart`.
- `r_volumetricFogFar <units>` — froxel range; `0` uses the automatic default.
- `r_volumetricFogAnisotropy <g>` — fallback Henyey-Greenstein phase asymmetry.
- `r_volumetricFogTemporal 0 | 1` — temporal accumulation.
- `r_volumetricFogHistoryWeight <value>` — history weight.
- `r_volumetricFogSunScale <value>` — sun scattering multiplier.
- `r_volumetricFogDlightScale <value>` — dynamic-light scattering multiplier.
- `r_volumetricFogStaticScale <value>` — baked-light scattering multiplier.
- `r_volumetricFogDlightShadows 0 | 1` — dynamic-light shadows inside fog; needs `r_dlightMode 2`.
- `r_volumetricFogBloom <value>` — add bright in-scattering to bloom.
- `r_volumetricFogReset 1` — gameplay hook to clear temporal history after a camera cut.

**Debug**
- `r_volumetricFogDebug 1..56` — density, lighting, history, media, particle, self-shadow, multiple-scattering and RGB-extinction views.
- `r_volumetricFogFreeze 0 | 1` — freeze the current volume and camera.
- `r_vfog` — inspect/control the active volumetric fog state.

Can significantly affect performance. Cost scales with froxel resolution, slice count, active lights and the optional effects below.

## Directional baked light in volumetrics

Preserves directional information from the non-sun part of the BSP light grid, so baked local lighting can scatter more strongly toward or away from the camera instead of being treated as fully isotropic.

**Requirements.** Requires froxel fog (`r_volumetricFog 2`).

**Main controls**
- `r_volumetricFogStaticDirectional 0 | 1` — isotropic baked local light / apply the phase function along its reconstructed baked direction.

Adds a few 3D light-grid fetches and phase calculations per participating froxel and may slightly affect performance.

## Height fog

Adds procedural ground haze to maps without requiring a BSP fog volume. Density is strongest around a base world height and falls exponentially upward.

**Requirements.** Requires froxel fog (`r_volumetricFog 2`).

**Main controls**
- `r_volumetricFogHeight 0 | 1` — disable / enable height fog.

**Other controls**
- `r_volumetricFogHeightOpaqueDistance <units>` — opaque distance at the base height.
- `r_volumetricFogHeightBase <z>` — base world height.
- `r_volumetricFogHeightFalloff <units>` — exponential scale height.
- `r_volumetricFogHeightMaxDensity <value>` — density cap below the base.
- `r_volumetricFogHeightTopHeight <units>` — optional soft upper cutoff.
- `r_volumetricFogHeightColor "r g b"` — scattering color.

Adds little overhead beyond the froxel fog pass itself.

## Volumetric density noise

Breaks uniform fog into world-space patches, clouds and drifting mist. A macro layer controls broad density variation and an optional detail layer adds smaller structure.

**Requirements.** Requires froxel fog (`r_volumetricFog 2`).

**Main controls**
- `r_volumetricFogNoise <bits>` — 1 height fog, 2 BSP fog, 4 global fog, 8 noise-enabled local volumes; bits can be combined.

**Other controls**
- `r_volumetricFogNoiseScale <units>` — macro-noise period.
- `r_volumetricFogNoiseContrast <value>` — macro contrast.
- `r_volumetricFogNoiseDetailScale <units>` — detail-noise period.
- `r_volumetricFogNoiseDetailContrast <value>` — detail strength; `0` avoids the second fetch.
- `r_volumetricFogNoiseWind "x y z"` — world-space drift speed.

May slightly to moderately affect performance depending on how much of the froxel volume contains noisy media.

## Local fog volumes

**For mod authors.** Small analytic spheres, ellipsoids and boxes can add smoke pockets, steam clouds or local haze without changing the BSP.

**Requirements.** Requires froxel fog (`r_volumetricFog 2`). The easiest persistent authoring route is the existing environment file `cubemaps/<map>/env.json`.

~~~json
{
  "FogVolumes": [
    {
      "Shape": "sphere",
      "Origin": [0, 0, 128],
      "Radius": 96,
      "Opaque": 250,
      "Color": [0.55, 0.55, 0.58],
      "Softness": 0.6
    }
  ]
}
~~~

Useful keys include `Shape`, `Origin`, `Radius` or `Size`, `Angles`, `Opaque`, `Color`, `Softness` and `Noise`. A runtime `GetRefFogVolumeAPI` is also available for engine/mod code.

**Debug / authoring tools**
- `r_fogvol` — print volume state and statistics.
- `r_fogvol add ...` — spawn a test volume.
- `r_fogvol test <count> [spread]` — deterministic stress test.
- `r_fogvol remove <index> | clear` — remove test volumes.
- `r_fogvol slices` — inspect per-slice lists.
- `r_fogvol dump` — print test volumes as an env.json `FogVolumes` array.

Cost depends on how many volumes overlap the visible froxel slices; isolated volumes are relatively inexpensive.

## Volumetric FX particles

**For mod authors.** Smoke, steam, dust and explosion-cloud particles can become real participating media instead of only transparent sprites. They then receive baked light, sun, dynamic lights, volumetric shadows and phase scattering naturally.

**Requirements.** Requires froxel fog (`r_volumetricFog 2`) and `r_volumetricParticles 1`. Nothing is detected automatically: a Particle or OrientedParticle must opt in.

~~~text
Particle
{
    ...
    volumetricMedia
    {
        extinction 0.04 0.06
        albedo 0.07 0.07 0.07
        radiusScale 1.0
        aspect 1 1 0.8
        softness 0.7
    }
}
~~~

**Main controls**
- `r_volumetricParticles 0 | 1` — disable / submit authored volumetric FX media.

**Other controls**
- `r_volumetricParticlesMax <count>` — maximum important particles uploaded per frame.
- `r_volumetricParticlesScale <value>` — extinction multiplier.
- `r_volumetricParticlesHistory <value>` — temporal-history retention where particle density changes.

**Debug**
- `r_volumetricParticlesDebug 0 | 1` — print culling statistics periodically.
- `r_volumetricFogDebug 26..28` — particle density, temporal-history reduction and proxy bounds.

**Binary note.** This is not renderer-only. SP adds a new cgame trap for volumetric particles and the engine forwards it to the renderer; MP submits them from the client FX system. Use the matching updated engine/client and game module.

Can moderately or significantly affect performance when many large volumetric particles overlap the view.

## Sprite particle lighting

Lights ordinary alpha-blended smoke and dust sprites from the same local light field used by froxel fog, so a smoke sprite in a dark room is dark and one beside a saber or lamp receives that color. Additive sprites remain unlit.

**Requirements.** Requires froxel fog (`r_volumetricFog 2`).

**Main controls**
- `r_particleLighting 0 | 1` — authored sprite color / volumetric-light-field lighting. Requires `vid_restart`.

**Other controls**
- `r_particleLightingingMix 0..1` — blend between authored and lit color.
- `r_particleLightingingScale <value>` — light-field gain.
- `r_particleLightingingFloor <value>` — minimum light factor.

**Debug**
- `r_particleLightingingDebug 1..5` — field, baked-only, sun-only, dynamic-only and sprite classification.

Moderate additional cost because the volumetric light field must exist and lit sprites sample it.

## Volumetric emission

**For mod authors.** Fog volumes and volumetric particles can emit light themselves, enabling glowing gas, fireballs and luminous smoke. Emission is added to the HDR volumetric result and can naturally enter bloom.

**Requirements.** Requires froxel fog. EFX particle emission additionally uses the volumetric-particle path.

For local `env.json` volumes, use `"Emissive": [r,g,b]` and optionally `"EmissiveDensity": d`. `"Opaque": 0` with an explicit emissive density creates a pure glowing volume.

~~~text
volumetricMedia
{
    extinction      0.004 0.006
    emissive        6 3 1.2
    emissiveDensity 0.02
    emissiveTint    1
}
~~~

**Main controls**
- `r_volumetricEmission <value>` — global volumetric emission scale; `0` disables it.

**Debug**
- `r_volumetricFogDebug 30..34` — scattering source, emissive source, combined source, integrated emission and history/emission separation.

Adds little overhead when no medium emits; active emitting media add extra injection work.

## Per-medium albedo and anisotropy

**For mod authors.** Different fog media can now have their own scattering color and phase anisotropy instead of sharing one global `g` value. Smoke, steam and atmospheric haze can therefore behave differently even when they overlap.

**Requirements.** Requires froxel fog.

BSP fog shaders can opt in:

~~~text
textures/test/medium_fog
{
    surfaceparm fog
    fogParms ( 0.6 0.6 0.65 ) 800
    fogAnisotropy 0.6
    fogAlbedo 0.9 0.4 0.3
}
~~~

Local fog volumes can use `"Anisotropy": 0.6` in env.json. EFX `volumetricMedia` can use `anisotropy 0.6`. Media without an override continue to use the global `r_volumetricFogAnisotropy` fallback.

**Debug**
- `r_volumetricFogDebug 35..39` — extinction, albedo, phase-lobe slots, mixed anisotropy and sun phase.

May slightly affect performance when several different media overlap.

## Volumetric self-shadowing

Makes dense fog, smoke and clouds attenuate light **inside the medium itself**. The far side of a dense cloud can therefore be darker than the side facing the sun even when no solid geometry blocks it.

**Requirements.** Requires froxel fog.

**Main controls**
- `r_volumetricSelfShadow 0 | 1 | 2` — off / sun / sun + strongest dynamic lights. Requires `vid_restart`.

**Other controls**
- `r_volumetricSelfShadowSamples <count>` — density samples along the sun ray.
- `r_volumetricSelfShadowDistance <units>` — maximum light-ray march.
- `r_volumetricSelfShadowOutsideHeightFog 0 | 1` — ignore / analytically include height fog beyond the march.
- `r_volumetricSelfShadowMaxLights <count>` — strongest dynamic lights self-shadowed in mode 2.

**Debug**
- `r_volumetricFogDebug 40..45` — density, optical depth, media transmittance and geometry/media shadow separation.

Can significantly affect performance because participating froxels trace additional rays through the media.

## Approximate multiple scattering

Returns part of the light removed by volumetric self-shadowing in dense high-albedo media. It prevents white steam or cloud interiors from becoming unrealistically black while keeping absorptive smoke much darker.

**Requirements.** Requires `r_volumetricSelfShadow 1` or `2`. Without a media shadow there is no lost light to recover.

**Main controls**
- `r_volumetricMultiScatter 0 | 1 | 2` — off / sun / sun + self-shadowed dynamic lights.

**Other controls**
- `r_volumetricMultiScatterOctaves 1..3` — scattering orders beyond single scattering.
- `r_volumetricMultiScatterAttenuation <value>` — optical-depth scale per octave.
- `r_volumetricMultiScatterContribution <value>` — returned energy per octave.
- `r_volumetricMultiScatterPhase <value>` — anisotropy reduction per octave.
- `r_volumetricMultiScatterLength <units>` — characteristic dense-medium size.
- `r_volumetricMultiScatterShadowFill <value>` — limited fill into geometry shadows.

**Debug**
- `r_volumetricFogDebug 46..50` — single scattering, multiple scattering, combined result, ratio and optical depth.

Adds no extra pass or texture, but adds substantial math to self-shadowed froxels. The incremental cost is moderate and grows with octave count and dynamic-light mode.

## RGB extinction / wavelength-dependent absorption

**For mod authors.** Participating media can absorb red, green and blue light at different rates. This can model effects such as water losing red light with depth or colored smoke tinting transmitted backgrounds.

**Requirements.** Requires froxel fog. `r_volumetricFogRGBExtinction 1` also needs enough texture units; unsupported hardware falls back to scalar extinction.

BSP fog shaders can use:

~~~text
fogExtinctionColor 3 0.5 0.5
~~~

Local env.json volumes can use `"Extinction": [r,g,b]`. Height fog uses `r_volumetricFogHeightExtinction "r g b"`. FX particle media currently remain neutral.

**Main controls**
- `r_volumetricFogRGBExtinction 0 | 1` — scalar / per-channel extinction and transmittance. Requires `vid_restart`.

**Other controls**
- `r_volumetricFogHeightExtinction "r g b"` — relative RGB extinction for procedural height fog.

**Debug**
- `r_volumetricFogDebug 51..56` — RGB extinction, transmittance, color shift, difference heat, chroma and tail transmittance.

This is one of the heavier volumetric options: it adds about 24 bytes per froxel, extra 3D buffers and a second composite draw.

# Rain & wet surfaces

## Weather chunk culling

Skips rain/snow weather chunks that are outside the camera frustum. It is intended to produce exactly the same image while avoiding unnecessary weather work.

**Main controls**
- `r_weatherCull 0 | 1` — draw every weather chunk / cull invisible chunks.

**Debug**
- `r_weatherDebugChunks 1 | 2` — visualize chunk bounds; mode 2 also prints detailed bounds.

No visual cost and usually improves performance.

## Modern rain streaks

Replaces simple additive rain particles with lit, premultiplied streaks whose length follows velocity and wind. The streaks also fade near surfaces and at very small screen sizes.

**Requirements.** Visible only while the map's rain weather system is active.

**Main controls**
- `r_rainStreaks 0 | 1` — legacy / modern rain streaks.

**Other controls**
- `r_rainStreakWidth <value>` — streak width.
- `r_rainStreakLength <value>` — length multiplier.
- `r_rainStreakOpacity <value>` — opacity.
- `r_rainStreakLighting 0 | 1` — fixed brightness / light-grid + sun lighting.

**Debug**
- `r_rainStreakDebug 1..5` — coverage, distance, lighting, contact and variation views.

May slightly affect performance.

## Wet surfaces

Makes rain-exposed PBR surfaces darker, smoother and more reflective while roofs and other occluders remain dry. The response is material-aware: cloth mainly darkens, while armor and metal can become substantially glossier.

**Requirements.** Requires an active rain map. Uses the weather occlusion depth map.

**Main controls**
- `r_weatherWetness 0 | 1` — disable / enable wet materials. Requires `vid_restart`.

**Other controls**
- `r_weatherWetnessStrength 0..1` — overall wetness.
- `r_weatherWetnessRoughness <value>` — scale of class-specific smoothing.
- `r_weatherWetnessDarkening <value>` — scale of class-specific albedo darkening.
- `r_weatherWetnessNormal <value>` — scale of class-specific normal flattening.
- `r_weatherWetnessEntityFacing <value>` — wetness of vertical character/prop faces.
- `r_weatherWetnessBias <units>` — rain-occlusion depth bias.

**Debug**
- `r_weatherSurfaceDebug 1..4` — exposure, wetness, roughness and dry/wet split.
- `r_weatherSurfaceDebug 16` — detected wet material class.
- `r_weatherSurfaceDebug 21` — geometric normal.

May slightly affect performance on rain-exposed PBR surfaces.

## Per-material weather response

**For mod authors.** Automatic rain response can be overridden for exceptional surfaces: a roof can get wet without holding puddles, cloth can avoid standing water, or an indoor decorative shader can stay completely dry.

**Requirements.** The corresponding wetness, puddle or runoff feature still needs to be enabled globally.

~~~text
textures/example/floor_tiles
{
    {
        map textures/example/floor_tiles
        weatherResponse 1 0
    }
}

textures/example/roof_slope
{
    weatherResponse 1.3 1 0.3
}
~~~

Syntax:

~~~text
weatherResponse <wetness> [<puddle> [<runoff>]]
~~~

Values are 0..4; 1 is the automatic response and 0 disables that channel. A single `weatherResponse 0` disables all weather response.

**Debug**
- `r_weatherSurfaceDebug 27..31` — enabled/excluded state, wetness/puddle/runoff scales and exclusion reason.
- `r_weatherMaterialList` — print drawn shaders with non-default response or automatic exclusion for one frame.

No meaningful performance cost.

## Procedural puddles

Creates standing water on flat rain-exposed world surfaces without editing the BSP. World-space noise breaks the water into irregular patches, and the resulting low roughness is automatically visible in direct light, cubemaps and SSR.

**Requirements.** Requires `r_weatherWetness 1` and active rain.

**Main controls**
- `r_weatherPuddles 0 | 1` — disable / enable procedural puddles.

**Other controls**
- `r_weatherPuddleCoverage 0..1` — approximate covered fraction.
- `r_weatherPuddleRoughness <value>` — water roughness.
- `r_weatherPuddleSlope "min max"` — geometric-normal range where puddles can form.
- `r_weatherPuddleScale <units>` — macro puddle pattern size.

**Debug**
- `r_weatherSurfaceDebug 5..10` — slope, noise, exposure, mask, roughness and eligibility.
- `r_weatherSurfaceDebug 13..15` — macro, micro and combined puddle masks.

May slightly affect performance.

## Height-aware puddles

Uses POM/material height data so water fills cracks and low regions before raised relief instead of simply applying one flat glossy mask.

**Requirements.** Requires procedural puddles and a material with height data.

**Main controls**
- `r_weatherPuddleUseHeightMap 0 | 1` — ignore / use material height data.

**Other controls**
- `r_weatherPuddleHeightSoftness <value>` — water-line transition width.
- `r_weatherPuddleWaterLevelBias <value>` — bias the virtual water level.

**Debug**
- `r_weatherSurfaceDebug 11..12` — material height and low/high regions.
- `r_weatherSurfaceDebug 15` — final combined puddle mask.

Adds little cost beyond puddles.

## Puddle ripples

Adds procedural expanding rain rings to standing water. The rings perturb the puddle normal, so SSR, cubemap reflections and direct highlights all react to them.

**Requirements.** Requires procedural puddles and active rain.

**Main controls**
- `r_weatherPuddleRipples 0 | 1` — disable / enable ripples.

**Other controls**
- `r_weatherPuddleRippleStrength <value>` — ring normal strength.
- `r_weatherPuddleRippleScale <units>` — cell/ring scale.
- `r_weatherPuddleRippleRate <value>` — ring cycles per second.

**Debug**
- `r_weatherSurfaceDebug 17..20` — ripple height, normal offset, masking and final normal.

May slightly affect performance.

## Water runoff

Adds a thin animated water film and streaks on rain-exposed slopes and walls. Flow follows gravity projected onto the surface, while puddles remain the preferred response on nearly horizontal faces.

**Requirements.** Requires `r_weatherWetness 1` and active rain.

**Main controls**
- `r_weatherRunoff 0 | 1` — disable / enable runoff.

**Other controls**
- `r_weatherRunoffStrength <value>` — film/streak strength.
- `r_weatherRunoffSpeed <units/s>` — flow speed.
- `r_weatherRunoffScale <units>` — streak-cell size.
- `r_weatherRunoffProbe <value>` — small outward rain-exposure probe for steep walls; `0` disables it.
- `r_weatherRunoffEntities 0 | 1` — also apply runoff to characters, props and movers.

**Debug**
- `r_weatherSurfaceDebug 22..26` — slope class, projected gravity, runoff mask, animated flow and final roughness.

May slightly affect performance. It is shading-only and has no fluid simulation or additional pass.

## Rain impact splashes

Creates short splash rings/spray where GPU rain drops cross the weather depth surface. No CPU trace is performed for each drop.

**Requirements.** Requires active rain and the weather depth map.

**Main controls**
- `r_rainSplashes 0 | 1` — disable / enable impact splashes.

**Other controls**
- `r_rainSplashSize <units>` — splash radius.
- `r_rainSplashLifetime <ms>` — lifetime.
- `r_rainSplashOpacity <value>` — opacity.

**Debug**
- `r_rainSplashDebug 1..3` — impact points, accepted/rejected crossings and recent trajectories.

Usually moderate only in very heavy rain; normal rain has a much smaller cost.

## Rain on the camera lens

Adds persistent beads, sliding droplets, trails and refraction on the camera while standing outside in rain. Existing drops fade and run off after moving under cover, and the pass is eventually skipped indoors.

**Requirements.** Requires HDR (`r_hdr 1`), active rain, the main world view and an outside camera position.

**Main controls**
- `r_rainLens 0 | 1` — disable / enable lens droplets. Requires `vid_restart`.

**Other controls**
- `r_rainLensDensity 0..1` — droplet density.
- `r_rainLensRefraction <value>` — refraction strength.
- `r_rainLensDropSize <value>` — droplet size relative to screen height.

**Debug**
- `r_rainLensDebug 1..4` — mask/trail film, normal, UV offset and scene/composition split; debug forces the effect on everywhere.

Moderate fullscreen post-processing cost while the effect is active; after the lens dries indoors the pass costs nothing.

# Foliage

## Automatic foliage classification

Recognizes likely leaf and plant surfaces in stock models so later foliage features can target vegetation without changing assets.

**Requirements.** No visual dependency; this is classification used by leaf flutter, plant wind and interaction.

**Main controls**
- `r_autoFoliage 0 | 1 | 2` — off / conservative stock-safe detection / broader experimental detection.

**Debug**
- `r_autoFoliageDebug 1 | 2` — classified surfaces / classified plus uncertain candidates.
- `r_autoFoliageList [filter]` — print detected foliage.

No measurable frame-time cost.

## Grass card geometry

Turns camera-facing grass billboards into fixed world-space crossed cards, reducing the “rotates with the camera” look and preventing blades from disappearing edge-on. It is especially visible on Yavin grass.

**Requirements.** Applies to existing surface-sprite grass.

**Main controls**
- `r_grassCardMode 0 | 1 | 2 | 3` — legacy / two-card cross / three-card tuft / adaptive three-to-two cards.

**Other controls**
- `r_grassCardLodDist <units>` — third-card fade distance in adaptive mode.
- `r_grassCardWidth <value>` — card width multiplier.

**Debug**
- `r_grassCardDebug 1..6` — card ID/direction, forced card counts and LOD visualization.

Moderate cost in dense grass because two or three cards increase geometry and alpha overdraw.

## Coherent foliage wind

Replaces synchronized circular grass sway with a world-space breeze field. Nearby plants share broad gusts while retaining smaller local motion.

**Requirements.** Applies to surface-sprite vegetation that already supports wind.

**Main controls**
- `r_foliageWind 0 | 1` — legacy sway / coherent breeze.

**Other controls**
- `r_foliageWindStrength <value>` — global strength.
- `r_foliageWindSpeed <value>` — time scale.
- `r_foliageWindDirection <degrees>` — world-space yaw.

**Debug**
- `r_foliageWindDebug 1..4` — exaggerated motion, bend color, frozen time and large-gust visualization.

May slightly affect performance; work is vertex-side only.

## Leaf flutter

Adds small high-frequency rustling to surfaces classified as leaves while leaving trunks and branches still. Stock Yavin leaf/vine cards are the main intended use.

**Requirements.** Requires `r_autoFoliage 1` or `2`.

**Main controls**
- `r_leafFlutter 0 | 1` — disable / enable leaf flutter.

**Other controls**
- `r_leafFlutterStrength <value>` — position amplitude.
- `r_leafFlutterSpeed <value>` — animation speed.
- `r_leafFlutterNormal <value>` — lighting-normal wobble.

**Debug**
- `r_leafFlutterDebug <bits>` — exaggerate, freeze, highlight fluttering surfaces and visualize displacement.

May slightly affect performance; it is small vertex work.

## Plant root wind

Adds gentle root-anchored bending to larger plant/fern models that previously remained completely static.

**Requirements.** Requires automatic foliage classification. Direction and speed come from the foliage wind controls.

**Main controls**
- `r_plantWind 0 | 1` — disable / enable plant root wind.

**Other controls**
- `r_plantWindStrength 0..4` — root-bend strength.

May slightly affect performance.

## Character / foliage interaction

Characters push grass and ferns away as they move through them. The renderer receives a few vertical body capsules and bends nearby vegetation around those capsules rather than around the camera.

**Requirements.** Fern interaction requires automatic foliage classification; grass interaction works on supported grass surfaces.

**Main controls**
- `r_foliageInteraction 0 | 1` — disable / enable character interaction.

**Other controls**
- `r_foliageInteractionStrength <value>` — bend strength.
- `r_foliageInteractionRadius <value>` — character capsule radius scale.
- `r_foliageInteractionMaxInteractors <count>` — uploaded character limit.
- `r_foliageInteractionNPCs 0 | 1` — player only / player + nearby NPCs and players.

**Debug**
- `r_foliageInteractionDebug <bits>` — capsules, exaggerated interaction, interaction-only view, contact heat, freeze and player-only modes.
- `r_foliageInteractionList` — print submitted/uploaded interactors.

**Binary note.** This is not renderer-only: cgame supplies character capsules through a new optional renderer interface. SP needs the updated `openjk_sp` executable and game/cgame module; MP needs the matching updated client/cgame path.

Moderate vertex cost in dense foliage, increasing with the number of nearby interactors.

## Persistent foliage bend field

Adds short physical-looking recovery after a character leaves a plant. Instead of snapping immediately back to the wind pose, recently touched vegetation springs toward rest with configurable damping and momentum.

**Requirements.** Requires `r_foliageInteraction 1` and therefore the same updated engine/game-module path.

**Main controls**
- `r_foliageBendField 0 | 1` — disable / enable persistent recovery.

**Other controls**
- `r_foliageBendFieldSize 64 | 128 | 256` — bend-field texture size. Requires `vid_restart`.
- `r_foliageBendFieldExtent <units>` — world size covered around the player.
- `r_foliageBendFieldStrength <value>` — field contribution.
- `r_foliageBendFieldRecoveryTime <seconds>` — approximate time back to rest.
- `r_foliageBendFieldDamping <value>` — spring damping; 1 avoids overshoot.
- `r_foliageBendFieldImpulse <value>` — extra momentum from moving through plants.

**Debug**
- `r_foliageBendFieldDebug <bits>` — field overlay, covered square, freeze, exaggeration and direct-vs-field isolation.
- `r_foliageBendFieldClear` — clear persistent bend state.
- `r_foliageInteractionList` — also prints current field state.

The default field uses about 256 KB for two 128×128 RGBA16F textures and one small update pass; expected frame cost is very small.

# Performance & loading

## Shader program cache

Caches linked Rend2 shader programs on disk so later starts and `vid_restart` operations can load driver binaries instead of recompiling every permutation.

**Requirements.** Requires driver support for `GL_ARB_get_program_binary` and at least one supported program-binary format. Unsupported drivers simply fall back to normal compilation.

**Main controls**
- `r_shaderProgramCache 0 | 1` — compile every time / use the on-disk program cache. Requires `vid_restart`.
- `r_shaderProgramCacheMaxMB <MB>` — maximum cache size; least-recently-used entries are evicted first.

The cache has no effect on frame performance. It can substantially reduce warm startup and `vid_restart` time. Driver or shader changes automatically invalidate incompatible entries.
