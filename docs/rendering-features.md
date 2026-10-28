# Jedi Academy 2026 rendering features

This page is a practical reference for rendering features added by Jedi Academy 2026. **Requirements** mention existing Rend2 settings or other features that must already be enabled. Command lists contain only controls introduced by the feature itself. Comparison modes and diagnostics are listed separately under **Debug**.

The [cvar and command provenance audit](rendering-cvar-audit.md) records every currently registered name absent from the local `rend2` baseline, with its introducing commit and verified `git blame` location. It includes engine/game support and internal status variables. Names are compared case-insensitively across SP, MP and shared code; inherited Rend2 controls can still appear here as requirements.

In the additional control tables, **Restart** means `CVAR_LATCH`: apply the setting with `vid_restart`. **Cheat** means `CVAR_CHEAT`; **Read-only** denotes engine-owned state. **Live** means the cvar is not latched; load-time settings still apply only when the resource next loads. Defaults describe registration in this source snapshot, not a recommended preset.

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

## Physical camera exposure

Exposes the HDR scene using ISO, f-number and shutter time, or an automatically metered EV100. `r_cameraMode 0` keeps legacy exposure; mode 1 is manual and mode 2 is physical auto. In physical modes `r_exposureCompensation` remains the artistic bias in stops.

**Requirements.** HDR and the existing tone-mapping path. `r_motionBlurPhysicalShutter 1` couples velocity-blur exposure to the camera shutter while a physical camera mode is active. Aperture controls exposure; it does not add depth of field. See [physical camera](rend2-physical-camera.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_cameraAperture` | `"16"` | Live | Physical camera aperture f-number N (manual mode; reserved for future physical DOF) |
| `r_cameraAutoEVMax` | `"20"` | Live | Maximum EV100 allowed in physical auto mode |
| `r_cameraAutoEVMin` | `"-6"` | Live | Minimum EV100 allowed in physical auto mode |
| `r_cameraDebug` | `"0"` | Cheat | Print calibrated camera/meter state once per second |
| `r_cameraISO` | `"100"` | Live | Physical camera ISO speed (manual mode) |
| `r_cameraMode` | `"0"` | Live | Camera exposure: 0 = legacy, 1 = physical manual, 2 = physical auto |
| `r_cameraShutterSeconds` | `"0.008"` | Live | Physical camera shutter duration in seconds (manual mode; optional motion-blur coupling) |
| `r_motionBlurPhysicalShutter` | `"0"` | Live | Use r_cameraShutterSeconds as motion-blur exposure duration in physical camera modes |

## SMAA, legacy glow and refraction

SMAA filters screen-space edges. Its quality and enable switches are latched. Generic refractive materials and distortion effects also expose a chromatic offset. Legacy dynamic glow can also add a bright-scene bloom contribution. These controls are independent of modern-water optics.

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_dynamicGlowBloom` | `"0.0"` | Live | Bright-scene contribution to legacy dynamic glow; active with r_bloom -1 and r_dynamicGlow enabled. |
| `r_refractionChromaticAberration` | `"0.05"` | Live | Chromatic offset in the generic refractive-material/distortion shader. |
| `r_smaa` | `"0"` | Restart | Disable/enable SMAA |
| `r_smaa_quality` | `"2"` | Restart | 0: LOW \| 1: MEDIUM \| 2: HIGH \| 3: ULTRA |

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

## Physical PBR units and emissive calibration

Gives lights and emissive surfaces one scene-linear unit contract. `r_pbrPhysical 1` enables it; geometry remains in JA world units. The unit scale and migration references are calibration settings, so changing them affects several lighting consumers together. A sun illuminance of 0 uses the map migration path.

See [physical PBR](rend2-physical-pbr.md) and [emissive materials](rend2-emissive-materials.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_emissiveDebug` | `"0"` | Cheat | Emissive debug: 1 provenance, 2 false-color nits, 3 confidence |
| `r_emissiveLegacyNits` | `"1000"` | Restart | Reference-white nits represented by legacy emissiveScale 1 in physical mode |
| `r_pbrLegacyReference` | `"1"` | Restart | Legacy light reference in canonical scene units at the migration anchor |
| `r_pbrPhysical` | `"0"` | Restart | 0 exact Rend2 compatibility, 1 unified scene-referred physical PBR contract (vid_restart) |
| `r_pbrPhysicalToScene` | `"0.0001"` | Restart | Canonical scene value per physical cd, lux or cd/m^2 |
| `r_pbrSunIlluminance` | `"0"` | Live | Physical sun illuminance in lux; 0 migrates the map sun |
| `r_pbrWorldUnitsPerMeter` | `"33.333333"` | Restart | JA world units per metre for physical light distance and emitter area |

**Commands**

- `r_pbrInfo` — Print physical PBR calibration and unit state.
- `r_pbrLight` — Inspect/test calibrated physical lights; run without arguments for syntax.

## GGX roughness, specular AA and energy compensation

Material roughness is perceptual; the BRDF converts it to GGX slope roughness at evaluation. The legacy migration switch preserves compatibility for older AutoPBR, spec-gloss and scalar inputs.

Specular AA increases effective roughness where normal variation would otherwise create unstable bright speckles. Energy compensation restores energy lost by the standard single-scatter GGX lobe; diffuse/specular energy partition remains active when compensation is disabled. These are separate controls.

See [roughness](rend2-ggx-roughness.md), [specular AA](rend2-specular-aa.md) and [energy compensation](rend2-brdf-energy-compensation.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_brdfEnergyCompensation` | `"1"` | Live | Multiple-scattering energy compensation for standard GGX; 0 uses single scattering while diffuse energy partition remains active |
| `r_pbrEnergyDebug` | `"0"` | Cheat | PBR energy: 1 diffuse, 2 specular, 3 total, 4 unreflected white-furnace energy, 5 split legacy diffuse (left) / conserved (right) |
| `r_pbrLegacyRoughness` | `"1"` | Live | Migrate legacy AutoPBR, spec-gloss and scalar alpha-like roughness to perceptual roughness; 0 provides an A/B strict interpretation |
| `r_specularAA` | `"1"` | Restart | Derivative and normal-mip variance filtering for GGX specular; vid_restart to compile it in |
| `r_specularAADebug` | `"0"` | Cheat | Specular AA debug: 1 material roughness, 2 material alpha, 3 effective roughness, 4 added roughness, 5 normal variance, 6 effective alpha |
| `r_specularAAMaxAdded` | `"0.25"` | Live | Maximum roughness specular AA may add (0..1) |
| `r_specularAAStrength` | `"0.15"` | Live | Specular AA normal variance scale (0..0.25) |

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

The console command `r_autoPOMSilhouette` changes `r_autoPOMSilhouetteMode`, the archived automatic-eligibility switch. Use the command to rebuild/reselect eligible loaded materials; the cvar is its stored mode. Requires `r_pomSilhouette 1` and ordinary height data.

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_autoPOMSilhouetteMode` | `"0"` | Live | Silhouette POM for every material with an ordinary POM height map (set with the r_autoPOMSilhouette command, needs r_pomSilhouette 1) |

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
- `r_sunShadowCache 0..3` — off / exact static BSP depth reuse / scrolling cascade clipmaps / clipmaps plus persistent world-space disk pages. Requires `vid_restart`.
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

## LTC shadowing and saber calibration

Static rectangular area lights can use cached world shadows, optionally including dynamic characters. Saber shadowing has separate approximation/reference modes. The saber luminance setting applies to physical PBR; its intensity multiplier also supports artistic tuning. Requires the corresponding LTC light feature.

See [LTC lighting](rend2-ltc-area-lights.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_ltcSaberIntensityScale` | `"1"` | Live | LTC saber surface lighting: radiance multiplier |
| `r_ltcSaberLuminance` | `"40000"` | Live | Physical mode: artistic saber blade luminance in cd/m^2 (nits) |
| `r_ltcSaberShadows` | `"-1"` | Live | Saber LTC shadows: -1 midpoint cube, 0 off, 1 anisotropic cube, 2 hybrid screen space, 3 three point reference |
| `r_ltcStaticShadows` | `"0"` | Live | Static rectangle LTC shadows: 0 off, 1 cached world, 2 cached world plus dynamic characters |

**Commands**

- `r_ltcShadowStats` — Print LTC shadow selection/cache statistics.

# Player & model lighting

## Entity light-grid lighting

Improves lighting across characters and entities. Instead of sampling one point for the whole model, Jedi Academy 2026 can sample several body positions or sample the BSP light grid per fragment.

**Requirements.** Uses the map's existing BSP light grid.

**Main controls**
- `r_entityLightGrid 0 | 1 | 2` — legacy / three CPU samples / per-fragment GPU samples. Requires `vid_restart`.

**Debug**
- `r_entityLightGridDebug 1..9` — ambient, direct, direction, validity, cell, old/new modes and difference. Requires `vid_restart`.

Mode 1 has little cost; mode 2 has moderate per-pixel cost on lit entities.

## Entity sun separation and original-grid probes

The legacy entity-grid path can separate authored sun from the grid's directed light and shadow that sun per pixel. A separate setting controls how much ambient grid light sun shadows darken.

Per-fragment grid mode can instead use L1 irradiance probes projected from the original BSP ambient/directed samples. These probes do not reconstruct physical emitters. See [entity lighting and removal boundaries](rend2-grid-reconstruction-removal.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_entityLightProbes` | `"1"` | Restart | Entity light grid L1 probes with r_entityLightGrid 2: first order irradiance from the original BSP direction and ambient/directed colours; no source reconstruction (vid_restart) |
| `r_entityShadowAmbient` | `"0"` | Live | Models (players, NPCs, items): share of the light grid ambient that the sun shadow darkens like the world lightmap (0 = legacy: only the directed light is shadowed, 1 = the whole ambient, as the lightmap). r_sunlightMode 1 only |
| `r_entitySunSplit` | `"0"` | Restart | Models (players, NPCs, items): take the sun out of the light grid's directed light and light it from the sun direction, shadowed per pixel by the sun cascades (0 = legacy: the grid light darkened by the sun shadow). Needs r_sunlightMode, the legacy grid (r_entityLightGrid 0) and normal or specular mapping; vid_restart |

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

## Hair and eye material models

Hair uses an anisotropic dielectric highlight aligned to authored or mesh tangents; eye coverage uses a smoother wet specular response. Mod shaders can declare `materialType hair` or `materialType eye` and a `materialMap`. Mixed face/eye atlases require a mask. Automatic hair selection is limited to exact hair-name tokens; painted eyes are not globally inferred.

There is no global enable cvar. `r_materialModelDebug 1..7` shows type, tangent, bitangent, anisotropy direction, hair specular, eye specular and mask. See [hair and eye materials](rend2-hair-eye-materials.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_materialModelDebug` | `"0"` | Cheat | Hair/eye debug: 1 = type, 2 = tangent, 3 = bitangent, 4 = anisotropy direction, 5 = hair specular, 6 = eye specular, 7 = mask |

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
- `r_ssrRoughOcclusion 0|1` — latched; 1 (default) applies the cubemap's reflection occlusion (lightmap scale, specular occlusion) to SSR on rough surfaces, removing light ghosts on shadowed walls.
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

## Rain suspension in froxel fog

Adds rain haze as another participating medium of the existing froxel volume. It uses the resolved rain subtype and intensity, and samples the top-down weather depth for every froxel: outdoor courtyards receive haze while rooms and the air below roofs do not. Sun, baked and dynamic lights (including saber, flashlight-like spot lights and explosions), shadows, HG phase and temporal reprojection are the normal froxel paths; there is no separate rain lighting pass. Acid rain uses the opt-in coherent renderer profile described in `docs/rend2-acid-rain-profile.md`.

**Requirements.** Requires active rain, its weather depth map and froxel fog (`r_volumetricFog 2`). The feature is off by default.

- `r_rainVolume 0 | 1` — disable / enable the rain medium.
- `r_rainVolumeStrength <value>` — normal-rain extinction per world unit at full normalized intensity.
- `r_rainVolumeHeavyMultiplier <value>` — additional heavy-rain density multiplier.
- `r_rainVolumeAnisotropy <g>` — rain medium HG anisotropy; default `0.72`.
- `r_rainVolumeGroundStrength <value>` — optional heavy-rain splash-mist extinction; `0` disables it.
- `r_rainVolumeGroundHeight <units>` — height of that layer above the exposed surface.
- `r_rainVolumeMaxDistance <units>` — camera range with a soft fade over the outer 15%.
- `r_volumetricFogDebug 71..74` — rain-only density, weather exposure, unattenuated in-scattering source and final integrated rain contribution.

## Baked light and sun separation in volumetrics

Froxel fog uses the original BSP light grid. When usable authored sun metadata is available, map-load preparation separates its sun contribution from the remaining baked light, so the existing sun visibility can shadow it independently. Without a usable split the ordinary merged grid remains available.

**Requirements.** Requires froxel fog (`r_volumetricFog 2`). `r_volumetricFogStaticScale` scales baked scattering.

The obsolete grid-gradient source reconstruction, recovered lamp directions and artificial light portals have been removed. See [the removal report](rend2-grid-reconstruction-removal.md). Canonical reconstructed-lighting controls are described separately below; no production solver or rendering adapter is installed.

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

**Requirements.** Requires froxel fog (`r_volumetricFog 2`) and `r_volumetricParticles 1`. Authored media uses a Particle or OrientedParticle opt-in; the optional legacy FX physicalization below can also supply automatic media.

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

## Automatic legacy FX physicalization

Adds automatic participating media to conservatively classified legacy smoke, steam, dust and gas effects. Mode/list changes affect the next FX update, including existing particles. Authored `volumetricMedia` keeps precedence.

**Requirements.** `r_volumetricFog 2`, `r_volumetricParticles 1` and matching updated game/client modules. Classification still gates effects in every mode. Emission, composite effects and automatic physical sources have independent opt-ins; the automatic system does not replace every legacy effect.

See [physicalization rules and source controls](legacy-fx-physicalization.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `fx_physicalization` | `"0"` | Live | 0 off, 1 all classified effects, 2 exact opt-in list, 3 all except opt-out list. |
| `fx_physicalizationAdaptive` | `"0"` | Live | Adapt the radius, softness and optical depth of already accepted media. |
| `fx_physicalizationAggregate` | `"0"` | Live | Experimental coalescing of exactly coincident automatic non-emissive media. |
| `fx_physicalizationComposite` | `"0"` | Live | Allow classified smoke within composite explosions. |
| `fx_physicalizationDebug` | `"0"` | Live | Print classification and submitted automatic-media diagnostics. |
| `fx_physicalizationEmission` | `"0"` | Live | Enable emission for exact stock flame/fireball profiles. |
| `fx_physicalizationOptIn` | `""` | Live | Exact normalized EFX names for mode 2, separated by spaces, commas or semicolons. |
| `fx_physicalizationOptOut` | `""` | Live | Exact normalized EFX names excluded by mode 3. |
| `fx_physicalizationSources` | `"0"` | Live | Enable the independently gated automatic physical-source submissions. |
| `fx_physicalizationStrength` | `"1"` | Live | Automatic-medium optical-depth multiplier; 0..16. |

**Commands**

- `fxaudit` — fxaudit <efx>: inspect physicalization eligibility; developer 1, SP.
- `fxsources` — Inspect the last automatic FX source submissions; developer 1, SP/MP.
- `r_volparticles` — Print particle-media submission, culling and upload limits.

## Sprite particle lighting

Lights ordinary alpha-blended smoke and dust sprites from the same local light field used by froxel fog, so a smoke sprite in a dark room is dark and one beside a saber or lamp receives that color. Additive sprites remain unlit.

**Requirements.** Requires froxel fog (`r_volumetricFog 2`).

**Main controls**
- `r_particleLighting 0 | 1` — authored sprite color / volumetric-light-field lighting. Requires `vid_restart`.

**Other controls**
- `r_particleLightingMix 0..1` — blend between authored and lit color.
- `r_particleLightingScale <value>` — light-field gain.
- `r_particleLightingFloor <value>` — minimum light factor.

**Debug**
- `r_particleLightingDebug 1..5` — field, baked-only, sun-only, dynamic-only and sprite classification.

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

## Legacy volumetric controls

The earlier ray-marched volumetric path also exposes a sample budget and persistent/default density scale. The temporary density multiplier is not an archived quality preset. These controls are distinct from froxel-grid resolution.

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_volumetricFogDefaultScale` | `"1.0"` | Restart | Scales volumetric fog density unless scale has been explicitly defined |
| `r_volumetricFogSamples` | `"48"` | Restart | How many ray samples to take |
| `r_volumetricFogScale` | `"1.0"` | Live | Temporarily scales volumetric fog density |

# Atmosphere & clouds

## Atmospheric scattering and sky

Adds long-range Rayleigh/Mie aerial perspective and an optional analytic sky before froxel fog. Sky mode 0 overlays the skybox, mode 1 blends it with the analytic sky and mode 2 replaces it. Sky-portal maps retain overlay behavior.

**Requirements.** A usable map sun or `r_forceSun`. The sun/air coefficients and the world-to-metre scale determine the result; aerial scale changes the apparent path length without rescaling geometry. See [atmosphere](rend2-atmosphere.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_atmosphere` | `"0"` | Live | Long range atmosphere: aerial perspective of distant surfaces and the sky (Rayleigh / Mie), composited before the froxel fog. Needs a map sun (or r_forceSun) |
| `r_atmosphereAerialScale` | `"1"` | Live | Atmosphere: multiplier of the aerial perspective path length only (1 = physical; JA views are a few hundred metres of air, 8-30 makes the haze visible) |
| `r_atmosphereAltitude` | `"0"` | Live | Atmosphere: altitude of the ground in metres above sea level |
| `r_atmosphereDebug` | `"0"` | Cheat | Atmosphere debug view: 1 Rayleigh in-scattering, 2 Mie in-scattering, 3 transmittance, 4 aerial in-scattering, 5 atmosphere-only sky, 6 froxel (red) + atmosphere (green) opacity, 7 LUTs, 8 aerial distance bands (1, 2, 5, 10, 20 km) |
| `r_atmosphereGroundZ` | `"auto"` | Live | Atmosphere: world z of the ground (altitude 0 + r_atmosphereAltitude), auto = lowest floor of the map |
| `r_atmosphereMie` | `"1"` | Live | Atmosphere: Mie (aerosol, haze) density multiplier |
| `r_atmosphereMieG` | `"0.8"` | Live | Atmosphere: Mie anisotropy g (Cornette-Shanks) |
| `r_atmosphereOzone` | `"1"` | Live | Atmosphere: ozone density multiplier (sky colour only) |
| `r_atmosphereRayleigh` | `"1"` | Live | Atmosphere: Rayleigh (air molecules) density multiplier |
| `r_atmosphereSky` | `"0"` | Live | Atmosphere sky: 0 overlay (skybox kept, horizon haze and sun glow), 1 blend skybox and analytic sky, 2 analytic sky (with the sun disc unless r_drawSun 1). Sky portal maps always use 0 |
| `r_atmosphereSkyBlend` | `"0.5"` | Live | Atmosphere sky 1: share of the analytic sky |
| `r_atmosphereStart` | `"0"` | Live | Atmosphere: distance in world units before which there is no aerial perspective |
| `r_atmosphereSunColor` | `"1"` | Live | Atmosphere sun illuminance: 0 map sun at the top of the atmosphere, 1 map sun at the ground (the air above the camera is undone), 2 white |
| `r_atmosphereSunGlow` | `"1"` | Live | Atmosphere sky 0: scale of the Mie sun glow over the skybox (lower it when the painted sun is elsewhere) |
| `r_atmosphereSunIntensity` | `"1"` | Live | Atmosphere: sun illuminance multiplier |
| `r_atmosphereSunSize` | `"0.53"` | Live | Atmosphere: angular diameter of the sun disc in degrees |
| `r_atmosphereUnitScale` | `"0.03"` | Live | Atmosphere: metres per world unit (0.03: the 64 unit player box is ~1.8 m) |

**Commands**

- `r_atmosphereInfo` — Print atmosphere configuration and runtime status.

## Volumetric clouds and cloud shadows

Ray marches an elevated cloud layer with sun self-shadowing, sky ambient and temporal accumulation. The cloud-shadow switch projects it onto world sunlight and volumetric sunlight.

**Requirements.** A usable map sun or `r_forceSun`. Enable/resolution changes require restart. Sky-portal rendering is opt-in and approximate. `r_cloudLegacy` controls how SP cloud-layer entities inform cloud selection. See [clouds](rend2-volumetric-clouds.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_cloudAmbient` | `"1"` | Live | Clouds: sky ambient multiplier |
| `r_cloudAnisotropy` | `"0.6"` | Live | Clouds: Henyey-Greenstein g of the forward lobe (silver lining towards the sun) |
| `r_cloudBase` | `"1500"` | Live | Clouds: altitude of the cloud base in metres above the ground (r_atmosphereGroundZ, r_atmosphereUnitScale) |
| `r_cloudCoverage` | `"0.5"` | Live | Clouds: 0 clear, 0.5 the weather map, 1 overcast |
| `r_cloudDebug` | `"0"` | Cheat | Clouds debug view: 1 interval, 2 max density, 3 shape noise, 4 detail noise, 5 sun transmittance, 6 phase, 7 temporal weight, 8 cloud radiance, 9 legacy mask (r) / coverage (g) / mode (b), 10 weather map, 11 cloud shadow map |
| `r_cloudDensity` | `"1"` | Live | Clouds: extinction multiplier (1 = 60 per km in a full cloud) |
| `r_cloudDetail` | `"1"` | Live | Clouds: erosion by the detail noise (0 = off, cheaper) |
| `r_cloudDetailScale` | `"1.5"` | Live | Clouds: size in km of one tile of the detail (erosion) noise |
| `r_cloudLegacy` | `"1"` | Live | Clouds: SP fx_cloudlayer entities: 0 ignored (legacy haze drawn), 1 style hint (tube: open sky overhead, alt: thin haze), 2 literal disc / ring at the entity height (debug) |
| `r_cloudMaxDistance` | `"60"` | Live | Clouds: march distance limit in km (faded out towards it) |
| `r_cloudMSOctaves` | `"3"` | Live | Clouds: multiple scattering octaves (1 = single scattering) |
| `r_clouds` | `"0"` | Restart | Volumetric clouds: a ray marched cloud shell above the map (sun, self shadow, sky ambient), composited between the atmosphere and the froxel fog. Needs a map sun (or r_forceSun); vid_restart |
| `r_cloudScale` | `"2"` | Restart | Clouds: march resolution divisor per axis (1, 2, 4), vid_restart |
| `r_cloudShadowExtent` | `"8"` | Live | Clouds: size in km of the cloud shadow map around the camera |
| `r_cloudShadowInterval` | `"1"` | Live | Clouds: frames between cloud shadow map updates |
| `r_cloudShadows` | `"0"` | Live | Clouds: shadows of the clouds on the world (sun of lightall, froxel fog sun) |
| `r_cloudShadowSteps` | `"5"` | Live | Clouds: light march steps towards the sun (self shadow); the shadow map uses twice as many |
| `r_cloudShapeScale` | `"12"` | Live | Clouds: size in km of one tile of the shape noise |
| `r_cloudSkyPortal` | `"0"` | Live | Clouds on sky portal maps: 0 off, 1 composited over the portal scene (approximate) |
| `r_cloudStepLength` | `"150"` | Live | Clouds: march step length in metres (the step count adapts to the path through the layer, at least 16, at most r_cloudSteps) |
| `r_cloudSteps` | `"64"` | Live | Clouds: max march steps per ray |
| `r_cloudTemporal` | `"0.9"` | Live | Clouds: weight of the reprojected history (0 = no temporal filter) |
| `r_cloudTop` | `"4000"` | Live | Clouds: altitude of the cloud tops in metres above the ground |
| `r_cloudWeatherScale` | `"40"` | Live | Clouds: size in km of one tile of the weather (coverage) map |
| `r_cloudWindDir` | `"45"` | Live | Clouds: wind direction in degrees (world x axis = 0, y = 90) |
| `r_cloudWindSpeed` | `"10"` | Live | Clouds: wind speed in metres per second |

**Commands**

- `r_cloudInfo` — Print cloud layer configuration and runtime status.
- `r_cloudLayerTest` — Exercise cloud-layer/legacy-entity selection.

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
- `r_rainDynamicLighting 0 | 1 | 2` — current grid/sun only / strongest nearby dynamic light / up to the limited nearby Forward+ cluster lights. This also lights impact splashes and keeps `r_rainStreakLighting`'s existing meaning.
- `r_rainDynamicMaxLights 1..8` — accepted dynamic lights per streak or splash in mode 2 (default 4). At most eight importance-ordered cluster/UBO candidates are examined; finite-radius attenuation and a contribution cutoff reject distant or negligible lights.

**Debug**
- `r_rainStreakDebug 1..5` — coverage, distance, lighting, contact and variation views.
- `r_rainDynamicDebug 1 | 2 | 3` — show the static/grid contribution, dynamic contribution, or normalized selected-light count.

Forward+ point/spot lights, saber line lights, blaster lights and explosion lights are read directly from the current renderer light data; rain has no separate light submission. Without an available cluster list, the same bounded loop reads the `Lights` UBO. Mode 0 returns the previous static result before evaluating any light. Use `r_speeds 100` and compare the existing **Weather draw** and **Weather splashes** GPU timers with mode 0/1/2 on both a light-rain and a heavy-rain map.

Mode 1 is intended for heavy rain. Mode 2 costs more per visible streak/splash but remains bounded independently of total scene-light count.

## Rain streak distance LOD

Hands distant rain from real streak geometry to the weather-occluded froxel medium. The stock distribution is nine 2000-unit square chunks around the camera: light, normal and heavy rain simulate 9,000, 18,000 and 45,000 drops respectively. Before this LOD, every visible chunk submitted its full count and streaks faded from 900 to 6000 units.

The near band keeps every streak. Across the middle band a stable per-particle hash reduces density to the configured floor and retained streaks receive inverse-density opacity compensation, capped at 4x to avoid saturated, aliased lines. Across that same wide band the streak contribution crossfades out while the rain medium reaches full weight. Beyond the middle distance no streak geometry is emitted.

**Requirements.** Requires an actually active rain volume (`r_rainVolume 1`, `r_volumetricFog 2`, weather depth and supported texture-unit budget). If the volume cannot render, LOD automatically falls back to all existing streaks so distant rain never disappears.

- `r_rainLOD 0 | 1` — master switch; default `1`.
- `r_rainLODNear <units>` — full-density radius; default `1200`.
- `r_rainLODMid <units>` — end of the streak/volume handoff; default `3600`.
- `r_rainLODDensityFloor 0.05..1` — lowest stochastic density in the middle band; default `0.25`.
- `r_rainLODFarVolumeCoupling 0..1` — `0` keeps the original all-distance medium, `1` fades the medium in across the LOD handoff.
- `r_rainLODDebug 0 | 1` — show green near, yellow-to-blue middle and blue far bands. Debug deliberately restores far streak markers.
- `r_speeds 100` — prints the current LOD/fallback state and the existing weather submission counts.

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

## Persistent wetness and drying

Stores rain exposure as world-surface water that accumulates during rain and dries after it stops. Steep surfaces and acid rain can use different accumulation/drying rates.

**Requirements.** `r_weatherWetness 1`. The persistent field and its resolution require restart. See [persistent wetness](rend2-weather-wetness-persistence.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_weatherWetnessAccumulationRate` | `"0.20"` | Live | Persistent world water deposited per second at full rain |
| `r_weatherWetnessAcidAccumulationScale` | `"1"` | Live | Acid-rain persistent wetness accumulation multiplier |
| `r_weatherWetnessAcidDryScale` | `"1"` | Live | Acid-rain persistent wetness drying multiplier |
| `r_weatherWetnessDryRate` | `"0.02"` | Live | Persistent world water removed per second (0.02 = about 50 seconds) |
| `r_weatherWetnessPersistence` | `"0"` | Restart | Persist world wetness after rain stops (requires r_weatherWetness) |
| `r_weatherWetnessPersistenceDebug` | `"0"` | Cheat | 1 shows persistent world water amount |
| `r_weatherWetnessResolution` | `"256"` | Restart | Persistent world wetness texture resolution (64-1024) |
| `r_weatherWetnessSteepDryScale` | `"2"` | Live | Drying multiplier for steep surfaces (selected from geometric normal) |
| `r_weatherWetnessUpdateHz` | `"10"` | Live | Persistent world wetness simulation updates per second |

## Unified precipitation and acid-rain optics

The resolved precipitation state is shared by streaks, haze, wetness, puddles, runoff and lens water. The acid-rain optical profile is optional and controls tint and strength for active acid rain; it does not create rain or change gameplay damage.

See [precipitation state](rend2-unified-precipitation.md) and [acid rain](rend2-acid-rain-profile.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_acidRainStrength` | `"1"` | Live | Strength of the coherent acid-rain optical profile |
| `r_acidRainTint` | `"0.78 1.00 0.62"` | Live | Linear RGB optical tint of acid rain |
| `r_acidRainVisuals` | `"0"` | Live | Coherent optical acid-rain profile for rain, wet surfaces, puddles, runoff, volume and lens water |

**Commands**

- `r_precipitationInfo` — Print resolved precipitation type, intensity and consumer state.

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

**Layered edge response (opt-in)**
- `r_weatherPuddleEdgeModel 0 | 1` — legacy response / continuous dry, damp fringe, thin film and standing-water response. Default `0` preserves the existing puddle shader.
- `r_weatherPuddleFringeDarkening 0..1` — additional albedo darkening at the damp fringe (default `0.18`).
- `r_weatherPuddleFilmRoughness 0.02..1` — target roughness of the thin film (default `0.18`); standing water continues to use `r_weatherPuddleRoughness`.

This model only splits the existing `puddleEdge` and `puddle` masks; it does not create or expand puddle placement. The damp fringe keeps the underlying normal and receives only a mild roughness reduction. The thin film progressively smooths roughness and partially flattens the normal, allowing the existing dielectric Fresnel, cubemap and SSR response to appear. The standing-water core reaches the geometric macro normal and retains rain-ripple perturbation. Existing per-material `weatherResponse` scaling still gates the whole response.

**Debug**
- `r_weatherSurfaceDebug 5..10` — slope, noise, exposure, mask, roughness and eligibility.
- `r_weatherSurfaceDebug 13..15` — macro, micro and combined puddle masks.
- `r_weatherSurfaceDebug 33` — four continuous region weights: dry grey, damp brown, thin film cyan, standing water blue.
- `r_weatherSurfaceDebug 34..36` — final roughness, added puddle darkening and final normal for the layered edge model.

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
- `r_weatherPuddleRipples 0 | 1 | 2 | 3` — off / procedural rings / real rain-impact field / real nearby impacts with procedural rings farther away.

**Other controls**
- `r_weatherPuddleRippleStrength <value>` — ring normal strength.
- `r_weatherPuddleRippleScale <units>` — cell/ring scale.
- `r_weatherPuddleRippleRate <value>` — ring cycles per second.

**Debug**
- `r_weatherSurfaceDebug 17..20` — ripple height, normal offset, masking and final normal.

May slightly affect performance.

## Rain-impact puddle simulation

Puddle ripple modes 2/3 inject real rain crossings into a bounded height/velocity field. Mode 3 blends to procedural rings beyond the nearby field. This is an impact-driven ripple simulation; it does not simulate puddle drainage or change puddle placement.

**Requirements.** Active rain, weather impact/depth resources and wet procedural puddles. See [water realism follow-up](rend2-water-realism-followup.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_weatherPuddleRippleDamping` | `"2.5"` | Live | Real puddle ripple damping per second |
| `r_weatherPuddleRippleDebug` | `"0"` | Cheat | Real ripple debug: 1 impulses, 2 height, 3 velocity, 4 final normal |
| `r_weatherPuddleRippleDistance` | `"1500"` | Live | Real ripple draw distance; hybrid fades to procedural beyond it |
| `r_weatherPuddleRippleFieldScale` | `"1"` | Live | World coverage of the real ripple field relative to the weather map |
| `r_weatherPuddleRippleImpulse` | `"0.45"` | Live | Velocity impulse deposited by each real rain impact |
| `r_weatherPuddleRipplePropagation` | `"10"` | Live | Real puddle ripple propagation in field texels per second |
| `r_weatherPuddleRippleResolution` | `"256"` | Restart | Real puddle ripple field resolution (64..1024; vid_restart) |

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

## Persistent runoff trails

Keeps runoff streaks dark and smooth after a water filament passes. A value of 0 disables retained trails.

**Requirements.** Persistent world wetness and runoff. See [runoff trails](rend2-weather-runoff-trails.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_weatherRunoffTrailDebug` | `"0"` | Cheat | Persistent world runoff trail debug: 1 retained history, 2 final trail mask |
| `r_weatherRunoffTrailPersistence` | `"0"` | Live | Seconds world runoff trails remain dark and smooth after water passes (0 off; needs persistent wetness) |

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

## Surface-dependent rain impacts

Chooses an impact response for hard dry/wet surfaces, puddles, real water, soil, vegetation and metal from the weather impact-class map. Each class has an independent strength multiplier.

**Requirements.** Active rain and rain-impact resources. The response switch needs restart; 0 keeps the legacy splash. See [realism follow-up](rend2-water-realism-followup.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_rainSplashHardDryStrength` | `"1"` | Live | Hard dry rain impact response scale |
| `r_rainSplashHardWetStrength` | `"1"` | Live | Hard wet rain impact response scale |
| `r_rainSplashMetalStrength` | `"1"` | Live | Metal rain impact response scale |
| `r_rainSplashPuddleStrength` | `"1"` | Live | Puddle rain impact response scale |
| `r_rainSplashSoilStrength` | `"1"` | Live | Soft soil rain impact response scale |
| `r_rainSplashSurfaceDebug` | `"0"` | Cheat | Show the top-down R8 weather impact class map |
| `r_rainSplashSurfaceResponse` | `"0"` | Restart | Surface-dependent rain impacts: 0 exact legacy splash, 1 class response (vid_restart) |
| `r_rainSplashVegetationStrength` | `"1"` | Live | Vegetation rain impact response scale |
| `r_rainSplashWaterStrength` | `"1"` | Live | Real water rain impact response scale |

## Rain on the camera lens

Adds persistent water on the camera lens:
- Beads that pin, merge and depin, sliding drops that leave translucent wet trails and residual beads, and rivulets and sheets in heavy rain.
- A burst of water when leaving a pool.

Rain only adds water while the camera is outside. Under cover, existing water keeps moving and dries. Light, normal, heavy and acid rain use different profiles, so heavy rain means turnover, film and rivulets rather than just more drops. See `docs/rend2-rain-lens.md`.

**Requirements.** Requires HDR (`r_hdr 1`) and the main world view. New rain needs active rain weather and an outside camera position.

**Main controls**
- `r_rainLens 0 | 1` — disable / enable lens water. Requires `vid_restart`.
- `r_rainLensQuality 0..2` — field resolution, drop count and defocus samples. Requires `vid_restart`.

**Other controls**
- `r_rainLensDensity 0..2` — rain input multiplier (default 1).
- `r_rainLensRefraction 0..4` — refraction strength.
- `r_rainLensDropSize 0.25..4` — drop size (geometry only).
- `r_rainLensFilm 0..2` — thin film / trail visibility.
- `r_rainLensBlur 0..2` — drop defocus.
- `r_rainLensReflection 0..2` — reflection of the environment (nearest cubemap) with Fresnel, and glints of the sun and nearby lights; 0 = refraction only.
- `r_rainLensInertia 0..2` — water lags behind strong camera acceleration (default 0).

**Debug**
- `r_rainLensDebug 1..9` — optical weight / blur, normal, UV offset, scene/final split, agents, film/wetness, pinning, transient state and controller panel; forces the pass on.
- `rainlens_event splash|spray|emerge`, `rainlens_profile light|rain|heavy|acid|auto`, `rainlens_clear`, `rainlens_stats`.
- Tuning cvars (cheat): `r_rainLensFieldHeight`, `r_rainLensAgentLimit`, `r_rainLensPinning`, `r_rainLensMerge`, `r_rainLensFilmDecay`, `r_rainLensWetDecay`, `r_rainLensHeavyFlow`, `r_rainLensPeripheralBias`; profiling: `r_rainLensPBO`, `r_rainLensMipBlur`.

**Maps and effects.** Waterfalls and spray: `LensWaterEmitters` in `cubemaps/<map>/env.json` or the efx `lensWater` primitive. Splashes of characters entering water nearby and leaving water yourself are automatic with the updated game modules. Scripts can use `r_we lenswater splash 1`.

The CPU simulation is bounded: up to 128 drops, 256 micro drops and 8 sheets, about 0.2 ms at worst. The field is quality bounded, and the full-resolution composite only samples extra where there is water. A dry lens under cover costs nothing.

## Film-first lens water

Adds a second lens-water model dominated by a draining refracting film and short-lived micro drops. Controls tune emergence from water, peripheral mist and directional impact splats. The default model remains the hybrid-drop model.

**Requirements.** The lens-water pass and HDR. See [lens water](rend2-rain-lens.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_rainLensEmergeTime` | `"1.0"` | Live | Lens water film-first model: how long the film takes to drain and break into drops leaving water, 0.25..4 (below 1: sooner) |
| `r_rainLensEmergeWater` | `"1.0"` | Live | Lens water film-first model: water left on the lens leaving water (drop count and size), 0.25..3 |
| `r_rainLensFilmMicro` | `"1.0"` | Live | Lens water film-first model: wet film micro refraction, 0..4 |
| `r_rainLensFilmModel` | `"0"` | Live | Lens water model: 0 = hybrid drops (A), 1 = film-first: short lived micro drops, refracting wet film (B) |
| `r_rainLensImpactSplat` | `"0"` | Live | Lens water film-first model: oblique rain impacts splat along their direction, throw spray and leave a smear, 0..2 (0 = off) |
| `r_rainLensMist` | `"0.04"` | Live | Lens water film-first model: rain reaching the lens when not looking into it (spray, mist), 0..1 |

# Water & liquids

## Modern water surfaces and body identity

Replaces classified stock water stages with Fresnel reflection, depth-aware refraction, absorption and direct-light glints before tone mapping. Bodies carry stable identity and optics through `cubemaps/<map>/env.json` metadata. Semantic BSP classification is the default; the name-based experimental fallback is separately gated.

**Requirements.** Enable `r_waterSurface 1` and restart. Water SSR is independent of opaque-surface `r_ssr`. Classification/override commands aid mod authors. See [water surfaces](rend2-water-surface.md) and [water bodies](rend2-water-bodies.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterSurface` | `"0"` | Restart | Modern water surface for the stock water (CONTENTS_WATER surfaces): Fresnel reflection (SSR -> cubemap), depth aware refraction, liquid absorption, before tone mapping. 0 = legacy water stages (no extra targets or passes); vid_restart |
| `r_waterSurfaceAbsorption` | `"1.0"` | Live | r_waterSurface: shared liquid extinction scale for surface and volume (r_volumetricWaterExtinction / Color, Slime) |
| `r_waterSurfaceDebug` | `"0"` | Cheat | r_waterSurface debug view: 1 = classified surfaces (blue water, green slime, violet fog volume medium, yellow override, magenta experimental), 2 = normal, 3 = Fresnel / reflection weight, 4 = path length under the surface, 5 = raw refracted color, 6 = SSR hit (green) / miss (red), 7 = reflection source (red SSR, green cubemap, blue fallback), 8 = transmittance, 9 = split old (left) / new (right), 10 = refraction depth rejection, 11 = roughness |
| `r_waterSurfaceDepthReject` | `"1"` | Live | r_waterSurface: refraction never samples objects in front of the water (shrinks the offset, else none) |
| `r_waterSurfaceDepthScale` | `"1.0"` | Live | r_waterSurface: scale of the path length under the surface (water thickness from the depth buffer) |
| `r_waterSurfaceExperimental` | `"0"` | Live | r_waterSurface: also classify surfaces by a water-like shader name (pool, lake, river, water...) when they face up and have no water contents; experimental |
| `r_waterSurfaceIOR` | `"1.333"` | Live | r_waterSurface: index of refraction of the liquid (Fresnel and refraction) |
| `r_waterSurfaceNormal` | `"1.0"` | Live | r_waterSurface: detail wave normal strength (0 removes micro detail; ambient macro is separate) |
| `r_waterSurfaceReflection` | `"1.0"` | Live | r_waterSurface: scale of the reflection (1 = physical) |
| `r_waterSurfaceRefraction` | `"1.0"` | Live | r_waterSurface: scale of the screen-space refraction offset (1 = physical for the depth behind the water) |
| `r_waterSurfaceRoughness` | `"0.06"` | Live | r_waterSurface: base roughness (perceptual); the unresolved waves add to it |
| `r_waterSurfaceSplit` | `"0.5"` | Live | r_waterSurfaceDebug 9: position of the split (0-1 of the view width) |
| `r_waterSurfaceSSR` | `"1.0"` | Live | r_waterSurface: weight of the screen-space reflections over the cubemap (independent of opaque r_ssr; 0 = cubemap only) |

**Commands**

- `r_waterBodies` — List resolved water-body identities and profiles.
- `r_waterInfo` — Inspect classified modern-water surfaces.
- `r_waterOverride` — Inspect/set water classification overrides; run without arguments for syntax.

## Participating liquid volumes

Treats liquid brushes as participating media. `r_volumetricWater` is a mask: 1 water, 2 slime, 4 lava; combine bits as needed. Each class has independent extinction, relative RGB absorption, scattering albedo and anisotropy.

**Requirements.** Froxel fog (`r_volumetricFog 2`). Updated cgame uses the renderer's active-liquid status to suppress duplicate screen tint. Surface sun attenuation also needs the sunlight path. Lava parameters describe absorption/scattering, without adding lava emission.

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `cg_underwaterTint` | `"1"` | Live | Legacy full-screen underwater tint; updated cgame skips liquid classes handled by volumetric water. |
| `r_volumetricLavaAlbedo` | `"0.6 0.15 0.02"` | Live | r_volumetricWater 4: lava single scattering albedo "r g b" (no emission: absorption and scattering only) |
| `r_volumetricLavaAnisotropy` | `"0.3"` | Live | r_volumetricWater 4: lava Henyey-Greenstein g |
| `r_volumetricLavaColor` | `"0.6 1.2 1.2"` | Live | r_volumetricWater 4: lava relative extinction "r g b" |
| `r_volumetricLavaExtinction` | `"0.02"` | Live | r_volumetricWater 4: lava extinction per world unit |
| `r_volumetricSlimeAlbedo` | `"0.25 0.7 0.2"` | Live | r_volumetricWater 2: slime single scattering albedo "r g b" |
| `r_volumetricSlimeAnisotropy` | `"0.4"` | Live | r_volumetricWater 2: slime Henyey-Greenstein g |
| `r_volumetricSlimeColor` | `"1.6 0.5 0.9"` | Live | r_volumetricWater 2: slime relative extinction "r g b" |
| `r_volumetricSlimeExtinction` | `"0.005"` | Live | r_volumetricWater 2: slime extinction per world unit; the slime profile is also the medium of water brushes under a slime-flagged surface (vjun1) and of env.json "Liquids" rules |
| `r_volumetricWater` | `"0"` | Restart | Froxel fog (r_volumetricFog 2): liquid brushes of the map are participating media, mask: 1 water, 2 slime, 4 lava (0 = off, the legacy look). cgame then skips the full screen tint of those liquids (r_volumetricWaterActive, cg_underwaterTint) |
| `r_volumetricWaterActive` | `"0"` | Read-only | Set by the renderer: liquid classes drawn as a medium on this map (1 water, 2 slime, 4 lava), read by cgame |
| `r_volumetricWaterAlbedo` | `"0.10 0.45 0.75"` | Live | r_volumetricWater: water single scattering albedo "r g b" (in-scattering color = albedo * relative extinction) |
| `r_volumetricWaterAnisotropy` | `"0.75"` | Live | r_volumetricWater: water Henyey-Greenstein g (forward scattering) |
| `r_volumetricWaterCausticFocus` | `"48"` | Live | r_volumetricWater: depth below the surface where the caustics reach full contrast (world units) |
| `r_volumetricWaterCaustics` | `"0.35"` | Live | r_volumetricWater: strength of the sun caustics under water (0 = off, 1 = full pattern contrast); the average sunlight is kept |
| `r_volumetricWaterCausticScale` | `"160"` | Live | r_volumetricWater: caustic pattern period in world units |
| `r_volumetricWaterCausticSpeed` | `"0.06"` | Live | r_volumetricWater: caustic drift in pattern periods per second |
| `r_volumetricWaterColor` | `"2.0 0.75 0.25"` | Live | r_volumetricWater: water relative extinction "r g b" (normalized to mean 1; red is absorbed first): RGB transmittance with r_volumetricFogRGBExtinction, the sun path color in both modes |
| `r_volumetricWaterExtinction` | `"0.0014"` | Live | r_volumetricWater: water extinction per world unit (mean of the channels; 0.0014 = 1/e after ~700 units; as fogparms depthForOpaque D: extinction = 5.14 / D) |
| `r_volumetricWaterSunPath` | `"1"` | Live | r_volumetricWater: exact path of the sunlight through the liquid to the surface (deep water is darker and bluer), and in the froxel volume the path of the dynamic lights through the liquid; 0 = unattenuated |
| `r_volumetricWaterSurfaces` | `"1"` | Restart | r_volumetricWater: the sun on surfaces inside a liquid passes the liquid above them (transmittance, caustics); needs r_sunlightMode |

**Commands**

- `r_liquids` — Inspect resolved participating liquid brushes and profiles.

## Snell window and underwater optics

When seen from inside water, the surface refracts the scene above and becomes internally reflective beyond the critical angle. The optional optics modes also transport sunlight through the interface and, in mode 2, evaluate atmosphere along the air segment.

**Requirements.** Modern water. Enable `r_waterSnell` for the interface; `r_waterSnellOptics` requires restart. See [water optics decisions](rend2-water-realism-followup.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterSnell` | `"0"` | Live | r_waterSurface: seen from inside the liquid, the surface is the water -> air interface: Snell's window refracts the scene above, total internal reflection beyond the critical angle (exact Fresnel, IOR r_waterSurfaceIOR) reflects the scene in the liquid (SSR -> cubemap in the liquid -> the liquid) through the medium. 0 = the r_waterSurface view from inside unchanged |
| `r_waterSnellDebug` | `"0"` | Cheat | r_waterSnell debug view: 1 = crossing direction (blue air -> water, orange water -> air, magenta stripes: camera contents disagree), 2 = total internal reflection share (red) / transmits (green), yellow critical angle, 3 = exact Fresnel, 4 = refracted direction (black: TIR), 5 = reflection (red) / transmission (green) weights, 6 = reflection source (red SSR, green cubemap, blue liquid / fallback) |
| `r_waterSnellOptics` | `"0"` | Restart | Snell optics: 0 legacy, 1 underwater sun BTDF/eta transport, 2 also atmosphere through the air segment |

## Ambient waves and wind

Adds coherent body-local macro/medium waves. Quality chooses 2/4/8 wave terms; normal detail remains separate. Dispersion mode 1 uses authored speed and finite depth; mode 2 uses calibrated gravity for non-flowing water. Wind response crossfades wave sets using resolved outdoor wind.

**Requirements.** Modern water. Visible geometric displacement also needs water geometry; without it waves affect optics. `r_waterWaveChoppiness` is reserved for future horizontal displacement. See [waves](rend2-water-waves.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterWaveAmplitude` | `"1"` | Live | Ambient wave amplitude scale |
| `r_waterWaveChoppiness` | `"1"` | Live | Future ambient horizontal displacement scale |
| `r_waterWaveDebug` | `"0"` | Cheat | Ambient waves debug: 1 height, 2 macro normal, 3 micro normal, 4 combined, 5 profile, 6 depth mask, 7 displacement, 8 split, 9 multiplier, 10 legacy deform |
| `r_waterWaveDispersion` | `"1"` | Live | Wave dispersion: 0 legacy nondispersive, 1 authored-speed finite-depth, 2 calibrated physical for non-flowing water |
| `r_waterWaveGravity` | `"313.6"` | Live | Physical renderer water gravity in world units per second squared (dispersion mode 2 and variable-depth interaction) |
| `r_waterWaveLength` | `"1"` | Live | Ambient wave wavelength scale |
| `r_waterWaveMicro` | `"1"` | Live | Micro slope scale in addition to r_waterSurfaceNormal |
| `r_waterWaveQuality` | `"1"` | Live | Ambient waves: 0 = 2 terms, 1 = 4 terms, 2 = 8 terms |
| `r_waterWaves` | `"0"` | Live | Ambient macro/medium water waves; 0 keeps existing water |
| `r_waterWaveShallow` | `"1"` | Live | Dampen analytical waves using stable body mean depth |
| `r_waterWaveSpeed` | `"1"` | Live | Ambient wave speed scale |
| `r_waterWaveTime` | `"-1"` | Cheat | Ambient wave time: -1 running, >=0 fixed seconds |
| `r_waterWind` | `"0"` | Live | Wind-driven outdoor water response (requires r_waterWaves); 0 preserves existing waves |
| `r_waterWindDebug` | `"0"` | Cheat | Water wind debug: 1 resolved wind, 2 current wave direction, 3 target direction, 4 blend/source |
| `r_waterWindScale` | `"1"` | Live | Global scale for resolved wind response on water |
| `r_waterWindTransition` | `"4"` | Live | Seconds to crossfade old and target wind wave sets |

## Water geometry, tessellation and camera waterline

Builds a subdivided render mesh so waves and disturbances change the water silhouette. Budgets bound vertices per body and map. Optional GL 4.3 tessellation uses a coarser base mesh with distance-dependent detail.

**Requirements.** Modern water and restart. Tessellation also requires `r_gl43 1` and supported hardware. The camera-waterline hysteresis follows the displaced visual surface to reduce rapid enter/exit flicker. See [geometry](rend2-water-geometry.md) and [waterline](rend2-dynamic-waterline.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterGeometry` | `"0"` | Restart | Subdivided render-only water mesh; requires r_waterSurface and vid_restart |
| `r_waterGeometryBodyVerts` | `"250000"` | Restart | Maximum subdivided vertices per water body |
| `r_waterGeometryDebug` | `"0"` | Live | Water geometry debug view |
| `r_waterGeometryEdge` | `"64"` | Restart | Target maximum water mesh edge length in world units |
| `r_waterGeometryMapVerts` | `"1000000"` | Restart | Maximum subdivided vertices per map |
| `r_waterlineDebug` | `"0"` | Cheat | Visual waterline diagnostics: 1 = once-per-second state, 2 = state plus recent camera-relative-height graph |
| `r_waterlineHysteresis` | `"1.5"` | Live | Visual camera waterline enter/exit band in world units; fast crossings reduce the band |
| `r_waterTessellation` | `"0"` | Restart | Distance-adaptive GPU tessellation of the r_waterGeometry mesh (GL 4.3, r_gl43); coarser base mesh, requires vid_restart |
| `r_waterTessForce` | `"0"` | Cheat | Water tessellation level for every edge (0: by distance), for tests |
| `r_waterTessLodDistance` | `"0"` | Live | Distance from the camera (world units) with the finest water tessellation; beyond it edges grow by r_waterTessPixels |
| `r_waterTessMax` | `"16"` | Restart | Maximum water tessellation level (8, 16, 32): the base mesh is that much coarser; requires vid_restart |
| `r_waterTessMinEdge` | `"0"` | Live | Shortest tessellated water edge in world units: 64 = never finer than the static r_waterGeometry mesh, 0 = only r_waterTessMax limits the detail near the camera |
| `r_waterTessPixels` | `"96"` | Live | Target tessellated water edge length in pixels (smaller is denser and slower; waves too short for the mesh stay in shading) |

**Commands**

- `r_waterGeometryInfo` — Print subdivided/tessellated water geometry and budgets.

## Water disturbances and bathymetry

Simulates local height/velocity disturbances in bounded per-body fields. Bathymetry supplies body-local depth/confidence; variable-depth propagation can use it to slow waves in shallow water.

**Requirements.** Modern water and restart. Geometry is needed for displaced vertices; otherwise supported fields affect optics. Memory, texel and body budgets limit allocation. `r_waterImpulse` supplies a diagnostic impulse. See [interaction](rend2-water-interaction.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterBathymetry` | `"0"` | Restart | Build exact body-local liquid depth/confidence fields; requires water interaction and vid_restart |
| `r_waterBathymetryDebug` | `"0"` | Cheat | Print body-local bathymetry coverage and confidence audit |
| `r_waterInteraction` | `"0"` | Restart | Per-body interactive water disturbances; requires r_waterSurface and vid_restart |
| `r_waterInteractionDamping` | `"1"` | Live | Profile disturbance damping multiplier |
| `r_waterInteractionDebug` | `"0"` | Cheat | Interaction debug: 1 domain, 2 mask, 3 height, 4 velocity, 5 slope, 6 source, 7 foam, 8 active/sleep, 9 mapping |
| `r_waterInteractionMaxBodies` | `"8"` | Restart | Maximum bodies with interaction fields |
| `r_waterInteractionMaxTexels` | `"524288"` | Restart | Total interaction texel budget |
| `r_waterInteractionMemoryMB` | `"16"` | Restart | GPU interaction-field memory budget in MiB |
| `r_waterInteractionQuality` | `"1"` | Restart | Interaction density: 0=32, 1=16, 2=8 world units per texel |
| `r_waterInteractionSpeed` | `"1"` | Live | Profile disturbance propagation-speed multiplier |
| `r_waterInteractionStrength` | `"1"` | Live | Global water interaction strength multiplier |
| `r_waterInteractionVariableDepth` | `"0"` | Live | Use bathymetry and g*div(H*grad(height)) in interactive water |

**Commands**

- `r_waterImpulse` — Inject a diagnostic impulse at the aimed water body; optional strength and radius.
- `r_waterInteractionInfo` — Print allocated disturbance fields, masks and resource budgets.

## Directional flow and wall response

Advects body-local wave detail and disturbances with resolved flow. The optional spatial field turns/slows flow at banks; wall impact adds local pile-up, sloshing, foam and wet contact.

**Requirements.** Modern water. Spatial flow and impact are latched; impact needs the flow field. See [flow](rend2-water-flow.md) and [wall response](rend2-water-realism-pass2.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterFlow` | `"0"` | Live | Directional per-body water flow; 0 preserves the pre-flow water result |
| `r_waterFlowDebug` | `"0"` | Cheat | Water flow debug: 1 direction, 2 magnitude, 3 confidence/source, 4 advected detail, 5 field speed, 6 wall impingement, 7 field shore distance |
| `r_waterFlowDetail` | `"1"` | Live | Directional advection scale for medium and micro water detail |
| `r_waterFlowField` | `"0"` | Restart | Spatial per-body flow field baked at map load: slower water at banks, flow turning along walls (requires r_waterFlow 1) |
| `r_waterFlowImpact` | `"0"` | Restart | Flow running into a wall piles up, sloshes, foams and wets the wall (requires r_waterFlowField 1) |
| `r_waterFlowSpeed` | `"1"` | Live | Global multiplier for resolved per-body flow velocity |

**Commands**

- `r_waterFlowOverride` — Inspect/set per-body flow overrides; run without arguments for syntax.

## Persistent foam and object intersections

Maintains body-local foam with advection, diffusion and decay. Waterfalls, impacts/wakes, shoreline contacts and optional wave steepness contribute independent sources. Object intersection foam uses qualified opaque depth/normal contacts; the verification command compares contact classification.

**Requirements.** Modern water; persistent foam fields require restart. Persistent intersection response uses allocated interaction fields. See [intersection foam](rend2-water-intersection-foam.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterFoamAdvection` | `"1"` | Live | Foam advection multiplier for resolved body flow |
| `r_waterFoamDebug` | `"0"` | Cheat | Persistent foam debug: 1 concentration, 2 velocity, 3 sources |
| `r_waterFoamDecay` | `"0.55"` | Live | Persistent foam concentration decay per second |
| `r_waterFoamDiffusion` | `"0.12"` | Live | Mild foam diffusion in texels per second |
| `r_waterFoamField` | `"0"` | Restart | Persistent body-local water foam fields; requires r_waterSurface and vid_restart (never ocean foam) |
| `r_waterFoamImpact` | `"1"` | Live | Impact-pool and wake foam source strength (0 disables this source) |
| `r_waterFoamIntersection` | `"1"` | Live | Obstacle/intersection foam source strength (0 disables this source) |
| `r_waterFoamResolution` | `"128"` | Restart | Longest side of each active local foam field |
| `r_waterFoamShoreline` | `"1"` | Live | Enabled shoreline foam source strength (0 disables this source) |
| `r_waterFoamSteepness` | `"0"` | Live | Optional local wave-steepness foam source strength |
| `r_waterFoamWaterfall` | `"1"` | Live | Waterfall/turbulent-stream foam source strength (0 disables this source) |
| `r_waterIntersectionFoam` | `"0"` | Live | Opaque-object intersection foam on modern shallow water |
| `r_waterIntersectionFoamDebug` | `"0"` | Cheat | Show the raw depth/normal-qualified object intersection mask |
| `r_waterIntersectionFoamPersistence` | `"1.5"` | Live | Seconds for intersection foam in the interaction field to fade |
| `r_waterIntersectionFoamStrength` | `"1"` | Live | Object/water intersection foam strength |
| `r_waterIntersectionFoamWidth` | `"8"` | Live | World-space half-width of the object/water intersection band |

**Commands**

- `r_waterIntersectionVerify` — Verify GPU water/object intersection classification.

## Displaced shorelines and wet contact

Attenuates macro waves at rigid/shallow boundaries, softens water/geometry contact and retains a drying wet fringe after water retreats. Shore foam feeds the existing foam field.

**Requirements.** Modern water, water geometry and restart. See [shorelines](rend2-water-shoreline.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterShoreAttenuation` | `"96"` | Live | World distance over which macro waves recover from a rigid/shallow boundary |
| `r_waterShoreContactSoftness` | `"6"` | Live | World width of the water/geometry soft contact and overlap |
| `r_waterShoreDebug` | `"0"` | Cheat | Shoreline debug: 1 depth, 2 boundary distance, 3 attenuation, 4 displaced height, 5 rigid/contact class, 6 contact mask, 7 wet fringe, 8 foam source, 9 crack inspection |
| `r_waterShoreFoam` | `"1"` | Live | Profile-scaled shoreline source strength for the existing water foam field |
| `r_waterShoreline` | `"0"` | Restart | Displaced-water shoreline/contact treatment; requires r_waterSurface, r_waterGeometry and vid_restart |
| `r_waterShoreWetPersistence` | `"8"` | Live | Seconds for a retreated displaced waterline to dry |
| `r_waterShoreWetStrength` | `"0.8"` | Live | Water-contact wet-fringe strength through the existing material wetness response |

## Water entry splashes, wakes and rain

Updated SP/MP game/client code submits water entry/exit and projectile impacts, movement wakes and rain disturbances. Sparse rain ripples and aggregate micro disturbance share the real-water body classification.

**Requirements.** Matching updated modules. Impulses need interaction resources and foam needs its corresponding field. `r_waterSplashes` is off by default; wake controls do not enable the water renderer. See [physical splashes](rend2-physical-water-splashes.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterRainRippleDebug` | `"0"` | Cheat | Water rain debug: 1 impact mask, 2 source texture, 3 sparse field, 4 aggregate energy |
| `r_waterRainRippleDensity` | `"0.22"` | Live | Statistical fraction of real-water rain impacts injected as explicit ripples |
| `r_waterRainRippleImpulse` | `"0.55"` | Live | Velocity impulse scale of explicit real-water rain ripples |
| `r_waterRainRippleMicro` | `"1"` | Live | Aggregate rain micro-disturbance scale on real water |
| `r_waterRainRippleQuality` | `"1"` | Restart | Real-water rain field quality: 0=128, 1=256, 2=512 (vid_restart) |
| `r_waterRainRipples` | `"1"` | Live | Rain disturbances on classified real water bodies |
| `r_waterSplashDebug` | `"0"` | Cheat | Show and print physical splash events |
| `r_waterSplashes` | `"0"` | Live | Physical water entry, exit and projectile splashes |
| `r_waterSplashFoam` | `"1"` | Live | Physical splash foam scale |
| `r_waterSplashProjectiles` | `"1"` | Live | Projectile and thrown-saber water splashes |
| `r_waterSplashQuality` | `"1"` | Live | Physical splash particles: 0 low, 1 medium, 2 high |
| `r_waterSplashStrength` | `"1"` | Live | Physical splash impulse and spray strength scale |
| `r_waterWakeDebug` | `"0"` | Cheat | Print/draw movement-wake diagnostics. |
| `r_waterWakeFoamSpeed` | `"260"` | Live | Entity speed at which the wake foam response reaches its reference strength. |
| `r_waterWakeMaxEntities` | `"16"` | Live | Maximum wake-producing entities per frame. |
| `r_waterWakeMinSpeed` | `"35"` | Live | Minimum entity speed for wake submission, world units per second. |
| `r_waterWakes` | `"1"` | Live | Submit movement-driven water wakes from updated game/client code. |
| `r_waterWakeStrength` | `"1"` | Live | Wake impulse strength multiplier. |

## Surface-driven caustics

Caustic mode 2 derives focusing from the actual resolved water surface instead of the older procedural pattern. Source-mask bits select sun, dynamic point/spot and directed baked light independently. Directed baked mode requires direction evidence; its procedural fallback is explicit.

**Requirements.** Modern water for surface-driven evaluation; enabled liquid attenuation and the corresponding source path. The light-source mask is latched. See [caustics](rend2-surface-caustics.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterCausticsBakedMode` | `"0"` | Live | directional baked-light caustics: 0 off, 1 surface-driven, 2 original procedural pattern |
| `r_waterCausticsBakedStrength` | `"0.35"` | Live | strength of liquid attenuation and caustic modulation on the directional baked-light share |
| `r_waterCausticsDebug` | `"0"` | Cheat | surface caustics debug: 1 slope, 2 refracted ray, 3 focus, 4 weighted result, 5 projected receiver, 6 attenuation, 7 interaction |
| `r_waterCausticsFilter` | `"1"` | Live | receiver-depth footprint filter for surface-driven caustics |
| `r_waterCausticsLightMask` | `"1"` | Restart | caustic light sources: 1 sun, 2 dynamic point/spot, 4 directional baked light |
| `r_waterCausticsLocalMaxLights` | `"1"` | Live | maximum dynamic point/spot lights evaluated for water caustics per receiver |
| `r_waterCausticsMaxDepth` | `"2048"` | Live | maximum liquid path depth for surface-driven caustic contrast |
| `r_waterCausticsMode` | `"1"` | Live | underwater caustics: 0 off, 1 original procedural, 2 actual water-surface driven |
| `r_waterCausticsQuality` | `"1"` | Live | surface-driven caustic derivative quality: 0 32u, 1 16u, 2 8u base spacing |
| `r_waterCausticsSlope` | `"1"` | Live | surface slope contribution to surface-driven caustics |

## Waterfalls, mist and whitewater

Dedicated waterfall sheets use authored metadata or audited stock mappings, with optional geometry detail and optical strength by waterfall size. Base mist can feed the froxel volume and lens spray. Impact pools inject disturbances and foam; whitewater transports stream/rapid aeration.

**Requirements.** Modern water features as applicable. Waterfall sheets and whitewater require restart. Mist in the volume needs froxel fog; lens coupling needs lens water; impact response needs interaction and/or foam fields. See [waterfalls](rend2-waterfalls.md), [mist](rend2-waterfall-mist.md), [impact pools](rend2-waterfall-impact.md) and [whitewater](rend2-water-whitewater.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterfall` | `"0"` | Restart | Dedicated modern waterfall sheets selected by env.json Waterfalls metadata or the audited stock mapping; 0 restores exact legacy stages; vid_restart |
| `r_waterfallDebug` | `"0"` | Cheat | Waterfall debug: 1 classified, 2 flow, 3 along-fall, 4 displacement, 5 thickness, 6 raw refraction, 7 turbulence, 8 aeration, 9 foam source, 10 spray source, 11 impact region, 12 subdivided wireframe |
| `r_waterfallGeometry` | `"1"` | Restart | Render-only waterfall subdivision; BSP collision is unchanged; vid_restart |
| `r_waterfallHeavyStrength` | `"1"` | Live | Heavy/high-volume waterfall profile breakup, whitewater and spray strength |
| `r_waterfallImpact` | `"0"` | Live | Localized waterfall impact-pool ripples, turbulence and foam; requires r_waterInteraction and/or r_waterFoamField |
| `r_waterfallImpactDebug` | `"0"` | Cheat | Draw resolved waterfall impact regions and print resolution diagnostics |
| `r_waterfallImpactFoam` | `"1"` | Live | Waterfall impact-pool persistent foam injection strength |
| `r_waterfallImpactImpulse` | `"1"` | Live | Waterfall impact-pool central downward impulse strength |
| `r_waterfallImpactRadius` | `"1"` | Live | Waterfall impact-pool authored/inferred radius multiplier |
| `r_waterfallImpactTurbulence` | `"1"` | Live | Waterfall impact-pool stochastic secondary disturbance strength |
| `r_waterfallMediumStrength` | `"1"` | Live | Medium waterfall profile breakup, whitewater and spray strength |
| `r_waterfallMist` | `"0"` | Live | Waterfall base spray/mist generated from authored emitters or classified sheet impact edges |
| `r_waterfallMistBalance` | `"0.65"` | Live | Waterfall particle/mist balance: 0 keeps particle spray only, 1 uses full froxel mist |
| `r_waterfallMistDebug` | `"0"` | Cheat | Draw and list resolved waterfall mist emitters |
| `r_waterfallMistDensity` | `"1"` | Live | Waterfall suspended mist density scale |
| `r_waterfallMistLens` | `"0"` | Live | Couple nearby waterfall spray to the existing LensWater SPRAY input |
| `r_waterfallMistRadius` | `"1"` | Live | Waterfall mist and lens emitter radius scale |
| `r_waterfallQuality` | `"1"` | Live | Waterfall coherent breakup quality: 0 low, 1 medium, 2 high |
| `r_waterfallRefraction` | `"1"` | Live | Waterfall thin-sheet refraction strength |
| `r_waterfallSmallStrength` | `"1"` | Live | Small waterfall profile breakup, whitewater and spray strength |
| `r_waterfallWhitewater` | `"1"` | Live | Waterfall aeration and lit whitewater scale |
| `r_waterWhitewater` | `"0"` | Restart | Advected stream/rapid aeration; requires r_waterSurface and vid_restart |
| `r_waterWhitewaterDebug` | `"0"` | Cheat | Whitewater debug: 1 flow, 2 turbulence source, 3 final whitewater, 4 foam sources |
| `r_waterWhitewaterDecay` | `"1.1"` | Live | Advected whitewater turbulence decay per second |
| `r_waterWhitewaterFoam` | `"0.35"` | Live | Persistent surface-foam injection from whitewater |
| `r_waterWhitewaterStrength` | `"1"` | Live | Whitewater bulk optical strength |
| `r_waterWhitewaterThreshold` | `"0.45"` | Live | Turbulence source threshold for whitewater formation |

**Commands**

- `r_waterfalls` — List selected waterfall sheets and metadata.

## Underwater suspended particles

Adds drifting motes and micro bubbles around an underwater camera using a per-body clean/murky/dirty profile, optionally authored in env.json.

**Requirements.** A supported resolved water body and underwater view. See [particles](rend2-water-particles.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_waterParticles` | `"0"` | Live | Underwater suspended particles and micro-bubbles around the camera (0 off, 1 on) |
| `r_waterParticlesBubbles` | `"-1"` | Live | r_waterParticles: share of micro-bubbles 0..1, -1 = the body profile's |
| `r_waterParticlesDebug` | `"0"` | Cheat | r_waterParticles debug: 1 placement (opaque, cyan motes, yellow bubbles), 2 no fog in front, 3 print stats every second |
| `r_waterParticlesDensity` | `"1"` | Live | r_waterParticles: density scale of the body profile |
| `r_waterParticlesDistance` | `"160"` | Live | r_waterParticles: radius of the particle volume around the camera (world units) |
| `r_waterParticlesProfile` | `"0"` | Live | r_waterParticles: body profile, 0 auto (env.json WaterParticles, else clean), 1 clean, 2 murky, 3 dirty, -1 none |
| `r_waterParticlesSize` | `"1"` | Live | r_waterParticles: particle size scale |
| `r_waterParticlesSpeed` | `"1"` | Live | r_waterParticles: drift and bubble rise speed scale |

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

## Plant fronds (wind and contact)

Fern, bush and grass-card models bend frond by frond. Each frond (found automatically from the model) is a chain of
rigid segments that keeps its length: stiff at the base, soft at the tip, and much stiffer across the leaf than out of
its plane, so a frond pushed sideways swings around its base instead of stretching. Wind bends every frond, and a
character pushes a frond to one side as a whole (it never wraps around a leg). The fronds swing back with one small
overshoot after a character leaves. The simulation runs on the CPU for the nearby plants, and every pass (shadows,
fog, motion vectors) draws the same pose.

**Requirements.** Requires automatic foliage classification. Wind direction and speed come from the foliage wind
controls; contact needs `r_foliageInteraction 1`.

**Main controls**
- `r_plantWind 0 | 1` — disable / enable the breeze on the fronds.

**Other controls**
- `r_plantWindStrength 0..4` — breeze strength.
- `r_plantStiffness <value>` — frond stiffness (1 = default).
- `r_plantSimDistance <units>` / `r_plantWindDistance <units>` — full simulation / coarse simulation / at rest.
- `r_plantSimMaxPlants <count>` — plants simulated per frame.

**Debug**
- `r_plantDebug <bits>` — frond centerlines, segment or bone colors, contact sides, CPU cost.
- `r_plantSimInfo` — print the simulated plants; `r_autoFoliageList` also prints the frond skeletons.

Small CPU cost for the plants near the camera; the skinning is cheap vertex work.

## Character / foliage interaction

Characters push grass and ferns away as they move through them. The renderer receives a few vertical body capsules.
Only what a body actually touches moves: grass at the legs is pushed exactly out of the body, ferns are pushed frond
by frond (see Plant fronds). Grass blades curve from the root (a stiff base, a soft tip) instead of tilting as flat
cards, spring back after the character left, and keep a faint trampled trail for a few seconds.

**Requirements.** Fern interaction requires automatic foliage classification; grass interaction works on supported
grass surfaces.

**Main controls**
- `r_foliageInteraction 0 | 1` — disable / enable character interaction.

**Other controls**
- `r_foliageInteractionStrength <value>` — contact depth scale.
- `r_foliageInteractionRadius <value>` — character capsule radius scale.
- `r_foliageInteractionMaxInteractors <count>` — uploaded character limit.
- `r_foliageInteractionNPCs 0 | 1` — player only / player + nearby NPCs and players.
- `r_grassTrample 0..1` — how much of the bend trampled grass keeps; `r_grassTrampleRecovery <seconds>` — its recovery.
- `r_grassBendSegments 1..8` — segments of a near grass sprite (1 = flat cards); `r_grassBendLodDist <units>` — how
  far the curved strips are used.
- `r_foliageBendFieldSize 64 | 128 | 256` — grass state texture size. Requires `vid_restart`.
- `r_foliageBendFieldExtent <units>` — world size of the grass state around the player.
- `r_foliageBendFieldStrength <value>` — grass displacement scale.
- `r_foliageBendFieldRecoveryTime <seconds>` / `r_foliageBendFieldDamping <value>` / `r_foliageBendFieldImpulse
  <value>` — grass spring back.

**Debug**
- `r_foliageInteractionDebug <bits>` — capsules, exaggerated interaction, interaction-only view, grass contact heat,
  freeze and player-only modes.
- `r_foliageBendFieldDebug <bits>` — grass state overlay, covered square, freeze, exaggeration.
- `r_foliageBendFieldClear` — clear the grass state.
- `r_foliageInteractionList` — print submitted/uploaded interactors and the grass state.

**Binary note.** This is not renderer-only: cgame supplies character capsules through a new optional renderer
interface. SP needs the updated `openjk_sp` executable and game/cgame module; MP needs the matching updated
client/cgame path.

Small vertex cost in dense grass (one texture fetch per vertex and a short curve), the near grass strips add
vertices; the grass state is one small pass per frame (256x256 texels, 1.5 MB).

# Reconstructed lighting

## Canonical reconstructed-lighting controls and observations

These settings govern the canonical CPU asset, build policy and independent consumer requests. All default to 0. Build policy 0 is disabled, 1 cache only, 2 cache or build and 3 force rebuild; a changed policy applies at the next main-world map load.

**Current implementation.** The lifecycle/API exists, but complete source hashing, a production solver and rendering adapters are unavailable. Consumer switches cannot by themselves improve lighting. Policies 2/3 can prepare preserved original observations for research without publishing a fitted lighting asset. Observation/evidence/export commands inspect original data and retain confidence/availability boundaries.

See [controls and availability](rend2-reconstruction-controls.md), [luxels](rend2-luxel-observations.md), [observations](rend2-lightgrid-observations.md), [evidence](rend2-emitter-evidence.md) and [offline export](rend2-offline-observation-datasets.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_lightReconArea` | `"0"` | Live | Use canonical rectangle/tube emitters via area/LTC lighting; live request, requires usable area sources and an available area adapter |
| `r_lightReconBuild` | `"0"` | Live | Canonical lighting asset: 0 off, 1 cache only, 2 cache or build, 3 force build; next main-world load (vid_restart clears context and may reload the world) |
| `r_lightReconEntities` | `"0"` | Live | Use canonical irradiance for entities/Ghoul2; live request, requires prepared irradiance and an available entity adapter |
| `r_lightReconEnvironment` | `"0"` | Live | Use the unique resolved canonical sky/environment for IBL; live request, requires prepared incoming radiance and an available environment adapter |
| `r_lightReconFog` | `"0"` | Live | Use canonical incoming radiance for fog; live request, requires a prepared field and available fog adapter |
| `r_lightReconIndirect` | `"0"` | Live | Use canonical indirect irradiance; live request, requires prepared irradiance and an available indirect adapter |
| `r_lightReconParticles` | `"0"` | Live | Use canonical incoming radiance for particles; live request, requires a prepared field and available particle adapter |
| `r_lightReconShadows` | `"0"` | Live | Use canonical emitters and visibility for shadows; live request, requires prepared visibility and an available shadow adapter |
| `r_lightReconSpecular` | `"0"` | Live | Use canonical incoming radiance for specular; live request, requires a prepared field and available specular adapter |
| `r_lightReconSun` | `"0"` | Live | Use the unique resolved canonical sun; live request, requires known emission and an available sun adapter |
| `r_lightReconUse` | `"0"` | Live | Master use of canonical reconstructed lighting; live, requires each consumer opt-in, data and an available adapter |
| `r_lightReconWorldDiffuse` | `"0"` | Live | Use canonical physical emitters for world diffuse; live request, requires usable sources and an available world adapter |

**Commands**

- `r_lightReconEvidence` — Inspect classified emitter evidence and confidence.
- `r_lightReconExport` — Export a bounded original-observation dataset for offline analysis.
- `r_lightReconInfo` — Print world asset/build/consumer availability without starting a build.
- `r_lightReconLuxels` — Inspect original baked luxel-to-geometry mappings.
- `r_lightReconObservations` — Inspect preserved original BSP observations.

# Performance & loading

## Shader program cache

Caches linked Rend2 shader programs on disk so later starts and `vid_restart` operations can load driver binaries instead of recompiling every permutation.

**Requirements.** Requires driver support for `GL_ARB_get_program_binary` and at least one supported program-binary format. Unsupported drivers simply fall back to normal compilation.

**Main controls**
- `r_shaderProgramCache 0 | 1` — compile every time / use the on-disk program cache. Requires `vid_restart`.
- `r_shaderProgramCacheMaxMB <MB>` — maximum cache size; least-recently-used entries are evicted first.

The cache has no effect on frame performance. It can substantially reduce warm startup and `vid_restart` time. Driver or shader changes automatically invalidate incompatible entries.

## Modern OpenGL paths and light-list validation

Requests GL 4.3 and enables supported modern paths, with legacy fallback. Forward+ and froxel light lists can be built on compute; froxel tile size tunes the dynamic-light-list partition. These switches do not enable the lighting features themselves.

Verification commands compare CPU/GPU results and can be expensive. See [OpenGL 4.3](rend2-opengl43.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_forwardPlusCompute` | `"1"` | Live | Forward+: 1 = cluster lists built by a GL 4.3 compute shader when available (r_gl43), 0 = built on the CPU |
| `r_gl43` | `"1"` | Restart | Prefer GL 4.3 and allow modern paths; 0 forces legacy paths after vid_restart |
| `r_volumetricFogLightCompute` | `"1"` | Live | Build froxel dynamic-light cluster lists on GL 4.3 compute |
| `r_volumetricFogLightTile` | `"8"` | Live | Froxel fog: froxels per side of a dynamic light tile (4, 8, 16), see r_vfogLightStats |

**Commands**

- `r_forwardPlusVerify` — Compare GPU and CPU cluster lists.
- `r_vfogLightStats` — Print froxel dynamic-light-list sizes and costs.
- `r_vfogLightVerify` — Compare GPU and CPU froxel light-list construction.

## Shadow, SSR and sky-portal optimizations

Point/spot shadow caches conservatively reuse unchanged depth. Single-scene-walk and layered-cube submission reduce shadow overhead when supported. SSR can resolve its half-resolution hits to current-frame radiance once and reuse that resolve during upsampling; water has separate prepass and motion-reprojection controls. Sky-portal culling limits portal shading to visible main-view gaps.

Sun/point-shadow verification performs additional rendering/readbacks and is intended for diagnostics. See [sun-cache validation](rend2-sun-shadow-cache-validation.md), [SSR validation](rend2-ssr-validation.md) and [water surfaces](rend2-water-surface.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `r_shadowCache` | `"1"` | Live | Reuse unchanged point/spot shadow maps; conservative around animated world surfaces and dynamic casters |
| `r_shadowLayered` | `"1"` | Live | With r_shadowSinglePass 1: 1 = all 6 faces of a point light cube in one layered draw command when supported |
| `r_shadowSinglePass` | `"1"` | Live | Point light shadows: 1 = one scene walk per light, filtered per cube face (same depth as 0) |
| `r_shadowStats` | `"0"` | Cheat | Print per-frame CPU front-end / submit cost of shadow and camera views once per second |
| `r_shadowVerify` | `"0"` | Cheat | 1 = next frame, render every point light shadow cube both ways (r_shadowSinglePass 0 and 1) and print the depth texels that differ |
| `r_skyPortalCull` | `"1"` | Live | 1 = the skyportal view only shades the pixels the opaque world of the main view leaves uncovered |
| `r_ssrResolveCache` | `"1"` | Live | SSR: resolve half-res rays to radiance once per frame, then upsample (0 = per-pixel resolve) |
| `r_sunShadowBenchmark` | `"0"` | Cheat | Measure this many complete frame intervals, with GPU readbacks/stats disabled |
| `r_sunShadowVerify` | `"0"` | Cheat | Compare cached sun depth with a full render: 1 next scene, 2 continuous (slow readbacks) |
| `r_waterMotionDebug` | `"0"` | Cheat | Water receiver motion debug (1 validity, 2 current, 3 previous, 4 screen motion, 5 analytic, 6 interaction, 7 waterfall/legacy, 8 previous UV, 9 invalidation, 10 camera-only split) |
| `r_waterSSROptimize` | `"7"` | Live | r_waterSurface SSR prepass savings, bits: 1 = the prepass tessellates for its own (reduced) resolution, 2 = it drops occluded water before the wave shading, 4 = it runs a prepass-only program without the shading code; 0 = the 10-19 prepass (A/B) |
| `r_waterSSRWaveReprojection` | `"0"` | Restart | Use exact previous-rendered water geometry for temporal SSR reprojection |

## Shader compilation and material loading

Builds shader permutations on demand, optionally links uncached batches in helper processes, caches prepared textures on disk and predicts texture work on loading threads. World and non-world material DDC switches are independent; the world cache defaults off. Texture prefetch has a separate memory budget.

SP also exposes loose-image indexing, lazy menu assets and the memory-touch load stage. Checksums and nested CPU timings support comparisons; nested timing totals must not be added together. See [material DDC](rend2-material-ddc.md), [shader cache](rend2-shader-cache.md) and [map loading](rend2-map-load.md).

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `com_touchMemoryOnLoad` | `"1"` | Live | Touch process memory at map load; 0 skips that SP load stage. |
| `fs_looseImageIndex` | `"1"` | Live | Windows loose JPG/PNG/TGA negative-lookup index; rebuilt at map/search-path changes. |
| `r_genNormalMapsLazy` | `"0"` | Live | Experimental: generate normal maps (r_genNormalMaps) only for lit stages that need them |
| `r_imageChecksums` | `"0"` | Live | Write a hash of every loaded texture to imagechecksums/<map>.txt after registration |
| `r_loadPrefetch` | `"1"` | Live | Decode predicted textures on worker threads during map load |
| `r_loadPrefetchMB` | `"512"` | Live | Memory for textures decoded ahead of use, in MB |
| `r_loadProfile` | `"0"` | Live | Print nested map-load CPU timings from the engine, renderer and matching SP game module. |
| `r_loadThreads` | `"-1"` | Live | Worker threads for load-time texture work: -1 = automatic, 0 = none |
| `r_materialDDCObjects` | `"1"` | Live | Cache prepared NPC, object and other non-BSP textures on disk |
| `r_materialDDCWorld` | `"0"` | Live | Cache prepared BSP surface textures on disk |
| `r_shaderCompileProcesses` | `"-1"` | Live | Link batches of GLSL programs that are not cached in this many rend2_glslc helper processes (-1 = half the CPU threads, at most 8; 0 = in the game process only) |
| `r_shaderProgramLazy` | `"1"` | Restart | Build the lightall / generic / fog / velocity / refraction / surface sprite GLSL permutations when a level or a draw first needs them instead of all at startup |
| `r_uploadPBO` | `"1"` | Live | Upload textures through a persistently mapped pixel buffer |
| `ui_lazyMenuAssets` | `"1"` | Live | SP: defer menu asset loading until required. |

**Commands**

- `glslusage` — Print shader program/permutation usage.
- `imagechecksums` — Write loaded-image checksums for comparison.

## Integration status and capture diagnostics

The capability flags and active-liquid mask are written by the engine/renderer and read by game code. The `cl_renderer*` flags and `r_volumetricWaterActive` default to 0; runtime values report the active interface or liquid classes. Set user-facing feature controls to change behavior.

The offscreen-present setting is a separate capture aid for scripted hidden-window tests.

| Control | Default | Application | Meaning |
| --- | --- | --- | --- |
| `cl_rendererLensWater` | `"0"` | Read-only | Engine-owned flag: the active renderer exposes lens-water events. |
| `cl_rendererSpotLights` | `"0"` | Read-only | Engine-owned flag: the active renderer exposes the optional spot-light API. |
| `cl_rendererWaterInteraction` | `"0"` | Read-only | Engine-owned flag: the active renderer exposes water interaction events. |
| `r_ext_multisample_default_fb` | `"1"` | Read-only | Internal default-framebuffer multisample state; not a user quality setting. |
| `r_offscreenPresent` | `"0"` | Restart | Render the frame into an offscreen buffer and copy it to the window at swap, so screenshots work from a hidden window (scripted tests); requires vid_restart |
