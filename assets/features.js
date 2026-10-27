window.FEATURES = {
 "tonemap": {
  "group": "Color and post-processing",
  "title": "Tone mapping",
  "cvar": "r_toneMapMode 0|1|2",
  "how": "The scene is rendered into a 16-bit floating-point buffer. Auto exposure measures the logarithmic average luminance of that buffer and smooths it over time. A single fullscreen pass then applies exposure, the selected curve and the display encoding; refractive surfaces use the same output transform, so both always agree.",
  "sections": [
   {
    "title": "Tone mapping",
    "html": "<p>Changes how the HDR scene is compressed into the display range. The difference is easiest to see in bright skies, lamps, sabers, explosions and dark-to-bright transitions. ACES gives stronger cinematic contrast and highlight rolloff; AgX-like is more neutral while keeping bright detail.</p>\n<p class=\"req\"><strong>Requirements.</strong> Works only with HDR enabled (<code>r_hdr 1</code>) and tone mapping enabled. Auto exposure is recommended with HDR (<code>r_autoExposure 1</code>); exposure can also be adjusted with the existing <code>r_exposureCompensation</code> / <code>r_cameraExposure</code> controls.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_toneMapMode 0 | 1 | 2</code> — 0 legacy Rend2 filmic, 1 ACES fitted, 2 AgX-like.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_toneMapDebug 1</code> — legacy vs selected tone mapper split.</li><li><code>r_toneMapDebug 2</code> — legacy / ACES / AgX comparison.</li><li><code>r_toneMapDebug 3..5</code> — raw HDR, exposure-adjusted and exposure false-color views.</li></ul>\n<p>No measurable effect on frame performance.</p>"
   }
  ],
  "media": []
 },
 "grading": {
  "group": "Color and post-processing",
  "title": "Color grading",
  "cvar": "r_colorGrading 1",
  "how": "The <code>.cube</code> file is loaded into a 3D texture. After tone mapping, each pixel's color is used as a coordinate into that texture, and the looked-up color is blended with the original by the intensity setting.",
  "sections": [
   {
    "title": "Color grading LUTs",
    "html": "<p>Applies a 3D color lookup table after rendering, allowing different maps to have different palettes, contrast and mood. A map can automatically use <code>maps/&lt;map&gt;.cube</code>, while a user can override it with another <code>.cube</code> LUT.</p>\n<p class=\"req\"><strong>Requirements.</strong> No special rendering mode is required. LUT files use the common <code>.cube</code> format.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_colorGrading 0 | 1</code> — disable / enable color grading.</li><li><code>r_colorGradingLUT \"&lt;file&gt;.cube\"</code> — select a LUT manually; empty uses <code>maps/&lt;map&gt;.cube</code> if present; <code>*identity</code> is neutral.</li><li><code>r_colorGradingIntensity 0..1</code> — blend between the original image and the LUT.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_colorGradingCompare 0 | 1</code> — disable / enable split-screen comparison, original on the left.</li></ul>\n<p>May slightly affect performance.</p>"
   }
  ],
  "media": []
 },
 "linear": {
  "group": "Color and post-processing",
  "title": "Linear lighting",
  "cvar": "r_linearLighting 1",
  "how": "Textures and lightmaps are decoded from sRGB into linear values before lighting, so light is added and multiplied in physically proportional units. The result is encoded for the display only once, at the end of the frame, by the tone mapper.",
  "sections": [
   {
    "title": "Linear lighting",
    "html": "<p>Calculates lighting in linear light instead of directly in display-encoded sRGB. It mainly changes how lightmaps, dark gradients and bright lighting combine, and makes the pipeline more physically consistent with HDR.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires HDR (<code>r_hdr 1</code>) and tone mapping (<code>r_toneMap 1</code>).</p>\n<h4>Main controls</h4>\n<ul><li><code>r_linearLighting 0 | 1</code> — legacy / linear-light handling of non-HDR lightmaps. Requires <code>vid_restart</code>.</li></ul>\n<p>No measurable effect on frame performance.</p>"
   }
  ],
  "media": []
 },
 "bloom": {
  "group": "Color and post-processing",
  "title": "HDR bloom",
  "cvar": "r_bloom 1",
  "how": "Glow and emissive surfaces write into a dedicated target next to the HDR scene. That target, optionally with the brightest scene pixels, is downsampled into a resolution pyramid and blurred on the way back up. The blurred light is added to the linear HDR image before exposure and tone mapping, so bloom follows the real brightness of its source.",
  "sections": [
   {
    "title": "HDR bloom",
    "html": "<p>Adds soft glow around genuinely bright HDR sources such as sabers, emissive panels, explosions and intense lights. Unlike the legacy LDR glow path, it works from scene-linear brightness before tone mapping.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires HDR (<code>r_hdr 1</code>) and tone mapping (<code>r_toneMap</code> or <code>r_forceToneMap</code>).</p>\n<h4>Main controls</h4>\n<ul><li><code>r_bloom -1 | 0 | 1</code> — legacy glow / off / HDR bloom.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_bloomIntensity &lt;value&gt;</code> — bloom strength.</li><li><code>r_bloomThreshold &lt;value&gt;</code> — scene-linear brightness where optional scene bloom begins.</li><li><code>r_bloomKnee &lt;value&gt;</code> — softness around the threshold.</li><li><code>r_bloomScatter &lt;value&gt;</code> — how widely the bloom pyramid spreads.</li><li><code>r_bloomSceneIntensity &lt;value&gt;</code> — contribution from bright non-emissive scene pixels; <code>0</code> keeps bloom emissive-only.</li></ul>\n<p>May slightly affect performance.</p>"
   }
  ],
  "media": []
 },
 "motionblur": {
  "group": "Color and post-processing",
  "title": "Motion blur",
  "cvar": "r_motionBlur 1",
  "how": "It reuses the temporal data rend2 keeps for SMAA T2x: a per-pixel velocity buffer, the previous frame's view projection and the previous entity matrices and bones. Each pixel is blurred along its own motion vector, scaled by the shutter angle and clamped; very large one-frame jumps are treated as camera cuts and not blurred.",
  "sections": [
   {
    "title": "Velocity motion blur",
    "html": "<p>Adds camera and object motion streaks while keeping HUD and menus sharp. It uses per-pixel motion rather than simply blurring the whole frame, so rotating, running and moving characters produce different vectors.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires HDR (<code>r_hdr 1</code>).</p>\n<h4>Main controls</h4>\n<ul><li><code>r_motionBlur 0 | 1</code> — disable / enable velocity motion blur. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_motionBlurShutterAngle &lt;degrees&gt;</code> — exposure length.</li><li><code>r_motionBlurReferenceFps &lt;fps&gt;</code> — FPS the shutter angle refers to; <code>0</code> uses the current frame interval.</li><li><code>r_motionBlurMaxPixels &lt;pixels&gt;</code> — maximum streak length at 1080p.</li><li><code>r_motionBlurQuality 0 | 1 | 2</code> — low / medium / high.</li><li><code>r_motionBlurSamples &lt;count&gt;</code> — sample override; <code>0</code> uses the quality preset.</li><li><code>r_motionBlurViewModelScale &lt;value&gt;</code> — blur strength for first-person hands, weapons and saber.</li><li><code>r_motionBlurCutDistance &lt;units&gt;</code> — one-frame translation treated as a camera cut.</li><li><code>r_motionBlurCutAngle &lt;degrees&gt;</code> — one-frame rotation treated as a camera cut.</li><li><code>r_motionBlurShutterScale &lt;value&gt;</code> — gameplay exposure multiplier, used by effects such as Force Speed.</li><li><code>r_motionBlurReset 1</code> — gameplay hook that clears motion history after a cut.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_motionBlurDebug 1..5</code> — velocity, camera/object velocity, sample count and blur contribution.</li></ul>\n<p>Moderate GPU cost, increasing with quality and screen resolution.</p>"
   }
  ],
  "media": []
 },
 "gtao": {
  "group": "Lighting and shadows",
  "title": "GTAO and contact shadows",
  "cvar": "r_aoMode 2",
  "how": "Right after the depth prepass, fullscreen passes reconstruct positions from depth, search the horizon in several directions around each pixel and integrate the visible part of the hemisphere. The result is denoised and read by the main lighting pass. Contact shadows march a short ray toward the sun through the depth buffer to catch small occluders that shadow maps miss.",
  "sections": [
   {
    "title": "GTAO",
    "html": "<p>Adds stronger and more stable ambient occlusion in corners, wall/floor contacts, around props and under nearby geometry. The newer path also stores a bent normal so indirect diffuse and environment specular can prefer directions that are actually open.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires the depth prepass (<code>r_depthPrepass 1</code>). Starting the renderer with AO completely disabled and enabling it later requires <code>vid_restart</code> so the required buffers exist.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_aoMode -1 | 0 | 1 | 2</code> — follow legacy <code>r_ssao</code> / off / legacy SSAO / GTAO.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_aoApply -1 | 0 | 1</code> — automatic / legacy application / indirect-light-only application.</li><li><code>r_aoMultiBounce 0 | 1</code> — preserve more indirect light on bright surfaces.</li><li><code>r_aoLightmapFraction &lt;value&gt;</code> — share of baked light treated as indirect.</li><li><code>r_aoSpecOcclusion -1 | 0 | 1 | 2</code> — auto / scalar / Lagarde / bent-normal cone specular occlusion.</li><li><code>r_gtaoQuality 0..3</code> — low / medium / high / ultra.</li><li><code>r_gtaoHalfRes 0 | 1</code> — full / half resolution. Requires <code>vid_restart</code>.</li><li><code>r_gtaoRadius &lt;units&gt;</code> — world-space radius.</li><li><code>r_gtaoFalloff &lt;value&gt;</code> — distance falloff.</li><li><code>r_gtaoThickness &lt;value&gt;</code> — thin-occluder compensation.</li><li><code>r_gtaoPower &lt;value&gt;</code> — visibility contrast.</li><li><code>r_gtaoDenoise 0..3</code> — edge-aware denoise passes.</li><li><code>r_gtaoBentNormals 0..1</code> — bent-normal strength for indirect lighting.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_aoCompare 0 | 1</code> — split-screen legacy SSAO vs GTAO.</li><li><code>r_aoDebug 1..10</code> — AO, depth, normal, contact and bent-normal views.</li></ul>\n<p>Moderate GPU cost; half-resolution GTAO is substantially cheaper than full resolution.</p>"
   },
   {
    "title": "Screen-space contact shadows",
    "html": "<p>Adds short sun shadows that cascaded shadow maps often miss, such as feet touching the floor, thin edges and small gaps.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires realtime sunlight and the depth prepass. Enabling the feature after starting without its screen-space buffers requires <code>vid_restart</code>.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_contactShadows 0 | 1</code> — disable / enable sun contact shadows.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_contactShadowLength &lt;units&gt;</code> — maximum ray length.</li><li><code>r_contactShadowSteps &lt;count&gt;</code> — ray samples.</li><li><code>r_contactShadowThickness &lt;units&gt;</code> — assumed blocker thickness.</li><li><code>r_contactShadowStrength &lt;value&gt;</code> — shadow strength.</li><li><code>r_contactShadowSoft 0 | 1</code> — first-hit / softer depth-weighted result.</li></ul>\n<p>Moderate GPU cost, mostly proportional to screen coverage and ray steps.</p>"
   }
  ],
  "media": []
 },
 "forwardplus": {
  "group": "Lighting and shadows",
  "title": "Forward+",
  "cvar": "r_forwardPlus 1",
  "how": "The view frustum is split into clusters: screen tiles times logarithmic depth slices. Every frame the dynamic lights are assigned to the clusters they touch (with a compute shader on OpenGL 4.3), and the material shader reads the light list of its pixel's cluster. Shading stays forward; there is no G-buffer.",
  "sections": [
   {
    "title": "Forward+ dynamic lighting",
    "html": "<p>Replaces per-surface 32-light masks with clustered light lists, allowing many sabers, blasters and explosions to coexist without dropping lights. The view is divided into screen tiles and logarithmic depth slices, and each cluster keeps only relevant lights.</p>\n<p class=\"req\"><strong>Requirements.</strong> No other new feature is required. Features such as LTC area lights use Forward+ as a dependency.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_forwardPlus 0 | 1</code> — legacy dynamic-light masks / clustered Forward+.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_forwardPlusTileSize &lt;pixels&gt;</code> — screen tile size.</li><li><code>r_forwardPlusSlices &lt;count&gt;</code> — logarithmic depth slices.</li><li><code>r_forwardPlusNearSlice &lt;units&gt;</code> — depth covered by the first slice.</li><li><code>r_forwardPlusMaxLightsPerCluster &lt;count&gt;</code> — light cap per cluster.</li><li><code>r_forwardPlusMaxShadowLights &lt;count&gt;</code> — strongest dynamic lights allowed shadow cubes when <code>r_dlightMode 2</code> is active.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_forwardPlusDebug 1..9</code> — tiles, slices, clusters, light counts, overflow, shadows and selected light. Requires <code>vid_restart</code>.</li><li><code>r_forwardPlusDebugLight &lt;index&gt;</code> — selected light for debug mode 9.</li><li><code>r_forwardPlusStats</code> — print cluster statistics.</li><li><code>r_forwardPlusBenchmark [frames]</code> — benchmark the lighting path.</li><li><code>r_forwardPlusSpawnTestLights &lt;count&gt; [spread]</code> — spawn test lights.</li></ul>\n<p>Usually inexpensive and can improve many-light scenes; very dense light clusters still increase shading cost.</p>"
   }
  ],
  "media": []
 },
 "ssgi": {
  "group": "Lighting and shadows",
  "title": "Screen-space GI",
  "cvar": "r_ssgi 1",
  "how": "Dynamic lights and emissive surfaces write their outgoing light into a separate screen buffer. Rays are marched from each pixel through the depth buffer, gather that light where they hit, are accumulated over frames and denoised, and the result is added as diffuse indirect light. Baked lighting is not bounced again because the lightmaps already contain it.",
  "sections": [
   {
    "title": "Screen-space global illumination",
    "html": "<p>Bounces nearby <strong>dynamic diffuse light and physical emission</strong> through visible surfaces. A saber can light a wall directly and that wall can then cast a weaker colored bounce onto the floor. By default it does not bounce the already-baked map lighting a second time.</p>\n<p class=\"req\"><strong>Requirements.</strong> Does not require Forward+ or SSR. A depth prepass is strongly recommended for correct temporal reprojection of moving geometry.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_ssgi 0 | 1</code> — disable / enable SSGI. Requires <code>vid_restart</code>.</li><li><code>r_ssgiSource 0 | 1 | 2</code> — dynamic + emissive / emissive only / full scene, with mode 2 experimental.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_ssgiIntensity &lt;value&gt;</code> — indirect-light strength.</li><li><code>r_ssgiQuality 0..3</code> — low / medium / high / ultra.</li><li><code>r_ssgiRays &lt;count&gt;</code> — rays per traced pixel; <code>0</code> uses the quality preset.</li><li><code>r_ssgiSteps &lt;count&gt;</code> — ray-march steps; <code>0</code> uses the preset.</li><li><code>r_ssgiMaxDistance &lt;units&gt;</code> — bounce distance.</li><li><code>r_ssgiThickness &lt;units&gt;</code> — assumed depth-buffer surface thickness.</li><li><code>r_ssgiTemporal 0 | 1</code> — temporal accumulation.</li><li><code>r_ssgiHistoryWeight &lt;value&gt;</code> — history weight.</li><li><code>r_ssgiDenoise -1..4</code> — denoise passes; <code>-1</code> uses the preset.</li><li><code>r_ssgiHalfRes -1 | 0 | 1</code> — preset / full / half-resolution tracing; changing trace resolution takes effect after <code>vid_restart</code>.</li><li><code>r_ssgiHiZ -1 | 0 | 1</code> — preset / linear / hierarchical tracing.</li><li><code>r_ssgiEmissiveScale &lt;value&gt;</code> — physical emissive contribution.</li><li><code>r_ssgiGlowScale &lt;value&gt;</code> — legacy glow/auto-emissive contribution.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_ssgiCompare 0 | 1</code> — split screen without / with SSGI.</li><li><code>r_ssgiDebug 1..10</code> — hit, distance, raw GI, history, denoise, source and final GI views.</li><li><code>r_ssgiFreezeHistory 0 | 1</code> — freeze temporal history.</li></ul>\n<p>Can significantly affect performance, especially at high/ultra quality.</p>"
   }
  ],
  "media": []
 },
 "ambient": {
  "group": "Lighting and shadows",
  "title": "Ambient light on models",
  "cvar": "r_entityLightGrid 2",
  "how": "The stock renderer lights a whole model from one light-grid sample at its origin. Here the grid is sampled at several body positions on the CPU, or per fragment on the GPU, so a character standing half in shadow is lit accordingly. Diffuse IBL derives directional irradiance from the runtime cubemaps and uses it to modulate the ambient term per direction.",
  "sections": [
   {
    "title": "Entity light-grid lighting",
    "html": "<p>Improves lighting across characters and entities. Instead of sampling one point for the whole model, Jedi Academy 2026 can sample several body positions or sample the BSP light grid per fragment.</p>\n<p class=\"req\"><strong>Requirements.</strong> Uses the map's existing BSP light grid.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_entityLightGrid 0 | 1 | 2</code> — legacy / three CPU samples / per-fragment GPU samples. Requires <code>vid_restart</code>.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_entityLightGridDebug 1..9</code> — ambient, direct, direction, validity, cell, old/new modes and difference. Requires <code>vid_restart</code>.</li></ul>\n<p>Mode 1 has little cost; mode 2 has moderate per-pixel cost on lit entities.</p>"
   },
   {
    "title": "Diffuse IBL / directional ambient",
    "html": "<p>Makes BSP light-grid ambient less flat by modulating it with directional irradiance derived from runtime cubemap probes. Opposite sides of a character can receive different ambient energy even where no direct light is present.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires usable runtime cubemap probes.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_diffuseIBL 0 | 1</code> — disable / enable directional probe ambient. Requires <code>vid_restart</code>.</li><li><code>r_diffuseIBLStrength &lt;value&gt;</code> — modulation strength.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_diffuseIBLDebug 1..5</code> — irradiance, modulation, legacy/new ambient and selected probe.</li></ul>\n<p>May slightly affect performance.</p>"
   }
  ],
  "media": []
 },
 "arealights": {
  "group": "Lighting and shadows",
  "title": "Area lights",
  "cvar": "r_ltcAreaLights 1",
  "how": "Linearly Transformed Cosines turn the integral of a rectangle or line light over the GGX lobe into a lookup in two small precomputed tables plus a closed-form polygon integral. Area lights live in the Forward+ cluster lists. Lamp detection scans glowing surfaces at map load and fits rectangles to bright connected regions.",
  "sections": [
   {
    "title": "LTC area lights",
    "html": "<p><strong>For mod authors.</strong> Rectangular and line emitters can produce correctly shaped direct-light highlights instead of behaving like point lights. Static map lamps can add only specular while leaving their baked diffuse lightmap intact.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires <code>r_forwardPlus 1</code> and <code>r_ltcAreaLights 1</code>.</p>\n<p>A map can define <code>maps/&lt;map&gt;.arealights.json</code>:</p>\n<pre><code>{\n  &quot;lights&quot;: [\n    {\n      &quot;type&quot;: &quot;rect&quot;,\n      &quot;center&quot;: [0, 0, 128],\n      &quot;right&quot;: [1, 0, 0],\n      &quot;up&quot;: [0, 1, 0],\n      &quot;halfWidth&quot;: 64,\n      &quot;halfHeight&quot;: 8,\n      &quot;color&quot;: [1.0, 0.85, 0.7],\n      &quot;intensity&quot;: 4.0,\n      &quot;mode&quot;: &quot;static_specular&quot;\n    }\n  ]\n}</code></pre>\n<p>Rectangle lights use <code>center</code>, <code>right</code>, <code>up</code>, <code>halfWidth</code> and <code>halfHeight</code>. Line lights use <code>start</code>, <code>end</code> and <code>radius</code>. <code>mode</code> can be <code>static_specular</code>, <code>static_full</code> or <code>dynamic</code>.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_ltcAreaLights 0 | 1</code> — disable / enable LTC rectangle and line lights. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_ltcIntensityScale &lt;value&gt;</code> — global radiance multiplier.</li><li><code>r_ltcStaticDiffuse 0 | 1</code> — also add diffuse for <code>static_specular</code> lights.</li><li><code>r_ltcMaxLights &lt;count&gt;</code> — maximum map area lights used per scene.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_ltcDebug 1..8</code> — specular/diffuse, source mode, cluster counts, bounds, outlines, axes and strongest ID. Requires <code>vid_restart</code>.</li><li><code>r_ltcDebugLight &lt;id&gt;</code> — choose the highlighted map light; <code>-1</code> chooses the nearest.</li><li><code>r_ltcReloadLights</code> — reload the map file.</li><li><code>r_ltcList</code> — list loaded lights.</li><li><code>r_ltcNearest</code> — print the nearest light.</li></ul>\n<p>Moderate GPU cost where area lights affect many pixels.</p>"
   },
   {
    "title": "Automatic LTC lamp detection",
    "html": "<p>Makes LTC useful on stock maps without authoring an area-light JSON file. At map load the renderer scans glow, emissive and surfacelight surfaces, finds bright connected regions and fits rectangles to them.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires LTC area lights. An authored <code>maps/&lt;map&gt;.arealights.json</code> always overrides automatic detection.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_ltcAutoAreaLights 0 | 1 | 2</code> — off / confident lamp shapes / also looser candidates.</li></ul>\n<h4>Debug / authoring tools</h4>\n<ul><li><code>r_ltcReloadLights</code> — rerun automatic conversion when no authored file exists.</li><li><code>r_ltcExtractLights</code> — write detected candidates to <code>maps/&lt;map&gt;.arealights.generated.json</code> for review.</li></ul>\n<p>Detection costs time only at map load; per-frame cost is the same as the area lights that were selected.</p>"
   },
   {
    "title": "Saber line area lights",
    "html": "<p>Makes the actual saber blade a line emitter instead of representing the whole blade with one point light, producing longer and more naturally shaped highlights.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires <code>r_ltcAreaLights 1</code> and Forward+.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_ltcSaberAreaLights 0 | 1</code> — point-light sabers / LTC line-light sabers.</li></ul>\n<p><strong>Binary note.</strong> This is not renderer-only: saber geometry is submitted by cgame through a new optional renderer API. Full support needs the rebuilt engine and game/cgame modules together with the renderer.</p>\n<p>Moderate additional cost around visible sabers.</p>"
   }
  ],
  "media": []
 },
 "spotlights": {
  "group": "Lighting and shadows",
  "title": "Spot lights",
  "cvar": "r_spotLights 1",
  "how": "A dynamic light can be limited to a cone. It lights surfaces through the legacy dynamic light loop and through Forward+, renders one perspective shadow map, and is injected into the froxel fog with the same attenuation. A cookie is projected through the same frustum as the shadow and multiplies with it.",
  "sections": [
   {
    "title": "Spot lights",
    "html": "<p><strong>For mod authors.</strong> EFX lights can now be true spot lights instead of point lights. The same cone can light opaque surfaces, cast a projected shadow and become a visible volumetric cone in froxel fog.</p>\n<p class=\"req\"><strong>Requirements.</strong> Existing point lights are unchanged. Spot shadows require <code>r_dlightMode 2</code>. Volumetric cones additionally require froxel fog (<code>r_volumetricFog 2</code>).</p>\n<pre><code>Light\n{\n    size { start 700 }\n    rgb  { start 1 0.92 0.8 }\n    spot\n    {\n        innerAngle 16\n        outerAngle 26\n        direction 1 0 0\n        shadows 1\n    }\n}</code></pre>\n<h4>Main controls</h4>\n<ul><li><code>r_spotLights 0 | 1</code> — submit authored spots as ordinary point lights / as spot lights.</li><li><code>r_spotLightShadows 0 | 1</code> — disable / enable spot shadows.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_spotLightDebug 1..4</code> — cone list, shadow frustums, spot-only surface lighting and spot-only fog lighting.</li><li><code>r_spot add|attach|spin|noshadow|list|clear</code> — renderer-side test spots.</li><li><code>fxplay &lt;efx&gt; [distance|muzzle]</code> — developer test command for authored EFX spots.</li></ul>\n<p><strong>Binary note.</strong> Full EFX support is not renderer-only. SP adds renderer/cgame plumbing for spot lights; MP parses/submits them from the client FX system. Use the updated engine/client and game module together with the renderer.</p>\n<p>The cone itself is inexpensive; shadows and volumetric lighting can make spot lights moderately expensive.</p>"
   },
   {
    "title": "Spot-light cookies / gobos",
    "html": "<p><strong>For mod authors.</strong> A spot light can project a texture through its cone. The same pattern appears on surfaces and inside volumetric fog, while geometry shadows still multiply with the cookie rather than being replaced by it.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires an authored spot light. Point lights do not have a projection and cannot use cookies.</p>\n<pre><code>spot\n{\n    innerAngle 22\n    outerAngle 26\n    cookie textures/imperial/grate02\n    cookieRoll 0\n}</code></pre>\n<h4>Main controls</h4>\n<ul><li><code>r_spotLightCookies 0 | 1 | 2</code> — off / intensity-only cookie / colored cookie.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_spotLightCookieDebug 1..3</code> — projected UV, cookie factor, cookie multiplied by geometry shadow.</li><li><code>r_spot cookie &lt;index&gt; &lt;image|none&gt;</code> — assign a cookie to a renderer-side test spot.</li></ul>\n<p><strong>Binary note.</strong> SP cookie registration uses new engine/cgame plumbing, so authored EFX cookies need the updated engine and game module as well as the renderer.</p>\n<p>Adds a texture lookup to affected spot lights and may slightly affect performance.</p>"
   }
  ],
  "media": []
 },
 "sunshadows": {
  "group": "Lighting and shadows",
  "title": "Sun shadows",
  "cvar": "r_sunShadowMode 1",
  "how": "Cascades are fitted to bounding spheres and snapped to texels, so shadows don't shimmer when the camera moves, and neighbouring cascades blend across an overlap. PCSS estimates the blocker distance to widen the penumbra away from the contact point. The cache reuses static world depth between frames and only redraws moving casters.",
  "sections": [
   {
    "title": "Sun Shadows 2.0",
    "html": "<p>Improves cascaded sun shadows with stabilized cascade fitting, blended cascade transitions, better receiver bias and optional PCSS contact-hardening softness.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires the existing realtime sun-shadow path.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_sunShadowMode 0 | 1</code> — legacy / Shadows 2.0. Requires <code>vid_restart</code>.</li><li><code>r_shadowPCSS 0 | 1</code> — disable / enable PCSS.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_sunShadowCache 0..3</code> — off / exact static BSP depth reuse / scrolling cascade clipmaps / clipmaps plus persistent world-space disk pages. Requires <code>vid_restart</code>.</li><li><code>r_sunShadowAlphaCasters 0 | 1</code> — alpha-tested foliage/cutout casters and receivers. Requires <code>vid_restart</code>.</li><li><code>r_shadowCascadeBlend &lt;value&gt;</code> — cascade overlap.</li><li><code>r_shadowDepthBias &lt;units&gt;</code> — constant receiver bias.</li><li><code>r_shadowNormalBias &lt;value&gt;</code> — normal offset.</li><li><code>r_shadowSlopeBias &lt;value&gt;</code> — slope-dependent bias.</li><li><code>r_shadowReceiverBiasClamp &lt;units&gt;</code> — maximum receiver correction.</li><li><code>r_shadowPCSSQuality 0..2</code> — low / high / ultra.</li><li><code>r_shadowSunAngularDiameter &lt;degrees&gt;</code> — apparent sun size.</li><li><code>r_shadowPCSSMaxPenumbra &lt;units&gt;</code> — maximum penumbra size.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_shadowDebug 1..11</code> — cascades, depth, PCSS, contact shadows, bias, point shadows and Ghoul2 receivers.</li></ul>\n<p>Moderate GPU cost; high-quality PCSS on large shadowed screen areas can be expensive.</p>"
   },
   {
    "title": "Character shadow improvements",
    "html": "<p>Improves Ghoul2 self-shadowing consistency by controlling caster LOD, using a better dynamic-light cube bias and optionally softening screen-space contact shadows.</p>\n<p class=\"req\"><strong>Requirements.</strong> The corresponding sun, dynamic-light or contact-shadow feature must already be active.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_shadowCasterLod 0 | 1</code> — shadow-view LOD / camera-view LOD for Ghoul2 shadow casters.</li><li><code>r_dlightShadowBias 0 | 1</code> — legacy / texel-scaled dynamic-light shadow bias.</li><li><code>r_contactShadowSoft 0 | 1</code> — binary / soft contact-shadow sampling.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_shadowCasterStats 0 | 1</code> — print Ghoul2 caster and LOD statistics.</li><li><code>r_shadowDebug 10</code> — point-light shadow visibility.</li><li><code>r_shadowDebug 11</code> — Ghoul2 receiver/caster view.</li></ul>\n<p>Usually a small performance difference.</p>"
   }
  ],
  "media": []
 },
 "autopbr": {
  "group": "Materials and reflections",
  "title": "Automatic PBR",
  "cvar": "r_autoPBR 0|1|2",
  "how": "At material registration, legacy stages without authored PBR data are classified from shader and texture names into classes such as metal, cloth, leather, skin and plastic, each with its own constant parameters. Roughness variation is derived from local detail of the diffuse texture and stored as a generated map. Authored material data always wins.",
  "sections": [
   {
    "title": "Auto PBR material classification",
    "html": "<p>Makes legacy materials without authored PBR maps react more plausibly to lights and reflections. Jedi Academy 2026 can either apply one generic dielectric fallback or classify common materials such as metal, cloth, leather, skin and plastic from asset names.</p>\n<p class=\"req\"><strong>Requirements.</strong> Applies only to legacy lit materials without authored specular/ORM data. Authored material data always wins.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_autoPBR 0 | 1 | 2</code> — legacy fallback / generic dielectric / heuristic material classes.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_autoPBRDebug 1</code> — material classes.</li><li><code>r_autoPBRDebug 2</code> — source of the PBR parameters.</li><li><code>r_pbrDumpMaterials [used|all|auto|authored|gouraud|&lt;class&gt;]</code> — list registered material sources and values.</li></ul>\n<p>No meaningful frame-time cost; classification happens during material registration.</p>"
   },
   {
    "title": "Automatic roughness variation",
    "html": "<p>Adds subtle spatial roughness variation to diffuse-only materials instead of giving every pixel one constant class roughness. It derives small-scale variation from local diffuse detail while avoiding brightness-based “fake metal” or AO generation.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires <code>r_autoPBR 1</code> or <code>2</code>. Only legacy diffuse-only materials are modified; authored specular/ORM maps, explicit scalar material values and converted legacy specular shaders are left untouched.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_autoPBRRoughness 0 | 1</code> — constant class roughness / generate an automatic roughness map. Turning it on requires <code>vid_restart</code>.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_autoPBRDebug 3</code> — final generated roughness in grey; blue means the material is not using auto roughness.</li><li><code>r_pbrDumpMaterials</code> — also reports generated roughness maps and generation time.</li></ul>\n<p>The generated maps add some loading work and texture memory, but little additional frame-time cost.</p>"
   },
   {
    "title": "Legacy shiny material conversion",
    "html": "<p>Converts old <code>alphaGen lightingSpecular</code> / fake <code>tcGen environment</code> materials from the vertex-lit path to the per-pixel PBR path. Their existing specular mask is reused as spatial PBR information, reducing characteristic Gouraud-style highlights on old shiny assets.</p>\n<p class=\"req\"><strong>Requirements.</strong> Intended for legacy shaders using those old shine techniques.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_autoPBRConvert 0 | 1</code> — keep the old vertex-lit path / convert compatible shaders to per-pixel PBR. Requires <code>vid_restart</code>.</li></ul>\n<p>May slightly increase GPU cost on converted surfaces because they move to per-pixel lighting.</p>"
   },
   {
    "title": "Diffuse BRDF",
    "html": "<p>Changes the diffuse reflection model of standard PBR materials. Burley/Disney accounts for roughness and viewing/light angles while Lambert is the simpler legacy-style diffuse response.</p>\n<p class=\"req\"><strong>Requirements.</strong> Affects standard PBR diffuse lighting; cloth uses its own BRDF path.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_diffuseBRDF 0 | 1</code> — Lambert / Burley-Disney diffuse.</li></ul>\n<p>No measurable effect on frame performance.</p>"
   }
  ],
  "media": []
 },
 "pom": {
  "group": "Materials and reflections",
  "title": "Parallax occlusion",
  "cvar": "r_pomSilhouette 1",
  "how": "POM marches the view ray through the height field in texture space; the step count now depends on the view angle and fades out with distance. Self-shadowing marches a second ray from the hit point toward each light. Silhouette POM builds a thin shell of prisms over the surface, so the ray can leave the base polygon, change the outline and write displaced depth.",
  "sections": [
   {
    "title": "Adaptive POM traversal",
    "html": "<p>Improves ordinary Parallax Occlusion Mapping by using fewer samples when looking straight at a surface and more at grazing angles. It can also fade POM into normal mapping with distance.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires ordinary POM (<code>r_parallaxMapping 1</code>), normal mapping and a height source such as <code>normalHeightMap</code>, a discovered <code>_nh</code> texture or <code>heightMap</code>.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_pomAdaptiveSteps 0 | 1</code> — legacy fixed traversal / angle-adaptive traversal.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_pomMinSteps &lt;count&gt;</code> — linear samples at normal incidence.</li><li><code>r_pomMaxSteps &lt;count&gt;</code> — linear samples at grazing angles.</li><li><code>r_pomBinarySteps &lt;count&gt;</code> — final hit refinement.</li><li><code>r_pomFadeStart &lt;distance&gt;</code> — distance where relief begins flattening.</li><li><code>r_pomFadeEnd &lt;distance&gt;</code> — distance where POM becomes normal mapping only.</li></ul>\n<p>This can reduce POM cost at easy angles and long distances, but grazing views may use more samples.</p>"
   },
   {
    "title": "POM self-shadowing",
    "html": "<p>Adds shadows inside POM relief. Brick grooves, cracks and raised panels can shadow themselves under the sun and nearby dynamic lights instead of looking like unshadowed texture displacement.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires <code>r_parallaxMapping 1</code>, normal mapping and height data. Sun self-shadowing also needs realtime sunlight (<code>r_sunlightMode 1</code> or <code>2</code>).</p>\n<h4>Main controls</h4>\n<ul><li><code>r_pomSelfShadow 0 | 1</code> — disable / enable relief self-shadowing. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_pomSelfShadowLightMode 0..3</code> — sun only / sun + strongest local / sun + limited local lights / all local lights.</li><li><code>r_pomSelfShadowMaxLocalLights &lt;count&gt;</code> — local-light budget for mode 2.</li><li><code>r_pomSelfShadowSteps &lt;count&gt;</code> — samples per shadow ray.</li><li><code>r_pomSelfShadowStrength &lt;value&gt;</code> — global shadow strength.</li><li><code>r_pomSelfShadowBias &lt;value&gt;</code> — ray-start bias inside the height field.</li><li><code>r_pomSelfShadowSoftness &lt;value&gt;</code> — penetration-to-shadow scale.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_pomDebug 1..8</code> — height, displaced UV, hit depth, sun/local self-shadow, sample counts and distance fade. Requires <code>vid_restart</code> when enabling the debug permutation.</li><li><code>r_pomDebugFreezeLight 0 | 1</code> — freeze the sun direction for comparison.</li></ul>\n<p><strong>Mod author override.</strong> A material can use <code>pomSelfShadow &lt;0..1&gt;</code> and an explicit <code>heightMap</code>:</p>\n<pre><code>{\n    map textures/test/bricks\n    normalMap textures/test/bricks_n\n    heightMap textures/test/bricks_h\n    parallaxDepth 0.06\n    pomSelfShadow 0.8\n}</code></pre>\n<p>Can significantly affect performance on POM-heavy scenes, especially with several self-shadowed local lights.</p>"
   },
   {
    "title": "Silhouette POM",
    "html": "<p><strong>For mod authors.</strong> Ordinary POM cannot change the edge of a triangle. Silhouette POM builds a small shell around selected surfaces so deep relief can carve or extend the actual visible silhouette and write displaced depth.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires <code>r_pomSilhouette 1</code>, <code>r_parallaxMapping 1</code>, normal mapping, a height field, and a compatible opaque surface. It is best for rocks, broken stone, chunky panels and strong edge relief rather than ordinary floors.</p>\n<pre><code>textures/test/rock_edge\n{\n    pomSilhouette\n    pomSilhouetteDistance 384\n    pomSilhouetteSteps 32\n    {\n        map textures/test/rock_edge\n        normalHeightMap textures/test/rock_edge_nh\n        parallaxDepth 0.06\n        parallaxBias 0.5\n        rgbGen identity\n    }\n}</code></pre>\n<h4>Main controls</h4>\n<ul><li><code>r_pomSilhouette 0 | 1</code> — disable / enable shell rendering. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_pomSilhouetteDistance &lt;units&gt;</code> — shell range; farther surfaces use ordinary POM.</li><li><code>r_pomSilhouetteFade &lt;units&gt;</code> — crossfade width.</li><li><code>r_pomSilhouetteSteps &lt;count&gt;</code> — normal-angle ray samples.</li><li><code>r_pomSilhouetteMaxSteps &lt;count&gt;</code> — grazing-angle ray samples.</li><li><code>r_pomSilhouetteBinarySteps &lt;count&gt;</code> — hit refinement.</li><li><code>r_pomSilhouetteViewDependence &lt;value&gt;</code> — how quickly samples increase toward grazing angles.</li><li><code>r_pomSilhouetteShadows 0 | 1</code> — displaced shells cast into sun cascades.</li><li><code>r_pomSilhouetteContactShadows 0 | 1</code> — apply screen-space sun contact shadows to shell pixels.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_pomSilhouetteDebug 1..11</code> — shell, wireframe, walls, discarded pixels, virtual depth, sample count and comparison views.</li><li><code>r_pomSilhouetteInfo</code> — print shell counts, memory, fallbacks and last-frame statistics.</li></ul>\n<p>Can significantly affect performance because every visible shell pixel ray-traces the height field and writes displaced depth.</p>"
   },
   {
    "title": "Automatic silhouette POM",
    "html": "<p>Automatically enables silhouette POM for compatible materials that already have ordinary POM height data, so stock <code>_nh</code> assets can be tested without editing shader files.</p>\n<p class=\"req\"><strong>Requirements.</strong> <code>r_pomSilhouette 1</code> must already be active when the map is loaded. Materials still need ordinary POM-compatible height data and eligible geometry.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_autoPOMSilhouette 0 | 1</code> — disable / enable automatic mode globally.</li><li><code>r_autoPOMSilhouette &lt;shader&gt; 0 | 1 | default</code> — per-shader override; a trailing <code>*</code> acts as a prefix.</li><li><code>r_autoPOMSilhouette clear</code> — remove per-shader overrides.</li></ul>\n<h4>Debug / inspection</h4>\n<ul><li><code>r_autoPOMSilhouette list</code> — list active candidates and fallback reasons.</li><li><code>r_autoPOMSilhouette &lt;shader&gt;</code> — print matching shader state.</li></ul>\n<p>Per-shader switches are live, but shell data is prepared at map load. Performance is the same as explicit silhouette POM for surfaces that are actually enabled.</p>"
   }
  ],
  "media": []
 },
 "skin": {
  "group": "Materials and reflections",
  "title": "Skin",
  "cvar": "r_skinSSS 1|2",
  "how": "Skin surfaces are classified automatically and only their diffuse lighting is affected; specular, eyes and metal stay sharp. Mode 1 wraps the diffuse term around the terminator. Mode 2 writes skin diffuse into a separate buffer and blurs it in two separable passes with a kernel whose width follows depth, stopping at depth and normal edges.",
  "sections": [
   {
    "title": "Skin subsurface scattering",
    "html": "<p>Softens diffuse light across skin and optionally adds back-light transmission through thin areas. Mode 1 is a cheap wrapped-diffuse approximation; mode 2 is real screen-space diffusion of the skin diffuse term.</p>\n<p class=\"req\"><strong>Requirements.</strong> Mode 2 requires HDR (<code>r_hdr 1</code>). Skin is automatically classified, with optional shader overrides for mods.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_skinSSS 0 | 1 | 2</code> — off / wrapped diffuse / screen-space diffusion. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_skinSSSMixedHeads 0 | 1</code> — also process mixed <code>*_head</code> textures. Requires <code>vid_restart</code>.</li><li><code>r_skinSSSStrength 0..1</code> — share of skin diffuse replaced by the diffused copy.</li><li><code>r_skinSSSWidth &lt;mm&gt;</code> — diffusion radius.</li><li><code>r_skinSSSQuality 0..2</code> — 11 / 17 / 25 taps.</li><li><code>r_skinSSSWrap &lt;value&gt;</code> — mode-1 wrap width.</li><li><code>r_skinSSSFollowSurface &lt;value&gt;</code> — depth/normal edge stopping.</li><li><code>r_skinSSSTransmission &lt;value&gt;</code> — optional back-light term.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_skinSSSCompare 0 | 1</code> — split screen without / with SSS.</li><li><code>r_skinSSSDebug 1..8</code> — classification, mask, raw diffuse, blur passes, delta and radius.</li><li><code>r_skinSSSList</code> — print skin classification.</li><li><code>r_skinSSSKernel</code> — print the diffusion kernel.</li></ul>\n<p><strong>Mod author overrides.</strong> <code>skinScatter &lt;0..1&gt;</code> changes scattering strength and <code>skinMask &lt;image&gt;</code> supplies a per-pixel mask.</p>\n<p>Mode 1 has a small cost; mode 2 has moderate GPU cost from the two diffusion passes.</p>"
   }
  ],
  "media": []
 },
 "emissive": {
  "group": "Materials and reflections",
  "title": "Emissive surfaces",
  "cvar": "r_autoEmissive 1",
  "how": "Emission is a separate material channel, added after reflected light, AO and shadows, and written to the glow target used by bloom. Compatible stock glow and unlit additive stages are recognized on the CPU and exposed as emission sources, so SSGI and bloom see them without shader edits.",
  "sections": [
   {
    "title": "Emissive materials",
    "html": "<p><strong>For mod authors.</strong> Materials can now emit linear HDR radiance independently from reflected light. This is useful for lamps, screens, control panels and other surfaces that should feed HDR bloom and SSGI without making an entire diffuse stage glow.</p>\n<p class=\"req\"><strong>Requirements.</strong> No global enable cvar is required. HDR is recommended for intensities above 1.</p>\n<pre><code>{\n    map textures/example/panel_d\n    emissiveMap textures/example/panel_e\n    emissiveColor 1.0 0.25 0.05\n    emissiveScale 4.0\n}</code></pre>\n<ul><li><code>emissiveMap &lt;image&gt;</code> — emission texture; black texels do not emit.</li><li><code>emissiveColor &lt;r g b&gt;</code> — linear emission color.</li><li><code>emissiveScale &lt;value&gt;</code> — scalar HDR intensity.</li><li><code>emissiveScale &lt;r g b&gt;</code> — RGB intensity shorthand.</li></ul>\n<p>No meaningful cost on materials that do not use the keywords; emissive materials add only a small amount of shading work.</p>"
   },
   {
    "title": "Automatic emissive compatibility",
    "html": "<p>Lets existing stock-style glow and safe unlit additive stages act as physical emission sources without editing their shader files. This is useful for SSGI and HDR bloom on existing maps.</p>\n<p class=\"req\"><strong>Requirements.</strong> Works on compatible legacy <code>glow</code> stages and standalone unlit additive stages. The renderer deliberately rejects ambiguous lit/additive stages to avoid double lighting.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_autoEmissive 0 | 1</code> — legacy behavior / expose compatible legacy glow stages as emissive sources.</li></ul>\n<p>No meaningful effect on performance.</p>"
   }
  ],
  "media": []
 },
 "ssr": {
  "group": "Materials and reflections",
  "title": "Screen-space reflections",
  "cvar": "r_ssr 1",
  "how": "A hierarchical depth buffer is built from the scene, and reflection rays are traced through it at full or half resolution, then resolved by roughness and accumulated over frames. Where a ray finds the reflected surface on screen, SSR replaces part of the parallax-corrected cubemap reflection; elsewhere the cubemap stays. Sabers and effects are added analytically, so they reflect even when they are off screen.",
  "sections": [
   {
    "title": "Screen-space reflections",
    "html": "<p>Adds real scene reflections to glossy PBR surfaces while retaining cubemaps as the fallback for off-screen, rough or unreliable rays. Sabers and additive effects can also appear as analytic reflected emitters even when the source itself is outside the screen.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires specular mapping (<code>r_specularMapping 1</code>). HDR is strongly recommended; the non-HDR scene buffer clamps reflection energy.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_ssr 0 | 1</code> — disable / enable hybrid SSR + cubemap reflections. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_ssrQuality 0..3</code> — low / medium / high / ultra.</li><li><code>r_ssrSteps &lt;count&gt;</code> — ray-march override; <code>0</code> uses the preset.</li><li><code>r_ssrRefineSteps &lt;count&gt;</code> — hit-refinement override.</li><li><code>r_ssrMaxDistance &lt;units&gt;</code> — maximum ray length.</li><li><code>r_ssrThickness &lt;units&gt;</code> — assumed depth-buffer thickness.</li><li><code>r_ssrMaxRoughness &lt;value&gt;</code> — roughness where SSR fully fades to cubemap.</li><li><code>r_ssrRoughOcclusion 0|1</code> — latched; 1 (default) applies the cubemap's reflection occlusion (lightmap scale, specular occlusion) to SSR on rough surfaces, removing light ghosts on shadowed walls.</li><li><code>r_ssrEdgeFade &lt;value&gt;</code> — screen-edge fade.</li><li><code>r_ssrHalfRes -1 | 0 | 1</code> — preset / full / half-resolution tracing.</li><li><code>r_ssrHiZ -1 | 0 | 1</code> — preset / linear / hierarchical tracing.</li><li><code>r_ssrTemporal 0 | 1</code> — temporal accumulation. Requires <code>vid_restart</code>.</li><li><code>r_ssrTemporalWeight &lt;value&gt;</code> — history weight.</li><li><code>r_ssrBlendStrength &lt;value&gt;</code> — confidence/replacement scale.</li><li><code>r_ssrHitCache 0 | 1</code> — reuse still-valid previous-frame ray hits; pixels are periodically retraced.</li><li><code>r_ssrReceiverCull 0 | 1</code> — skip ray marching on pixels that cannot use SSR.</li><li><code>r_ssrEmitters 0 | 1</code> — analytic saber/blaster/effect reflections.</li><li><code>r_ssrEmitterIntensity &lt;value&gt;</code> — analytic emitter brightness.</li><li><code>r_ssrEmitterMaxRoughness &lt;value&gt;</code> — roughness limit for analytic emitters.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_ssrCompare 0 | 1</code> — cubemap-only / hybrid split screen.</li><li><code>r_ssrDebug 1..12</code> — material, hit, confidence, raw/cubemap/hybrid reflection, emitter and hit-cache views.</li></ul>\n<p>Moderate GPU cost at low/medium settings and potentially significant cost at high/ultra. Hit caching and early culling reduce the tracing cost.</p>"
   }
  ],
  "media": []
 },
 "vfog": {
  "group": "Volumetrics and sky",
  "title": "Volumetric fog",
  "cvar": "r_volumetricFog 2",
  "how": "Fog is stored in a 3D grid aligned with the camera frustum (froxels). Each froxel gets density from the BSP fog volumes and other media, and in-scattered light from the light grid, the sun with its cascaded shadows and the dynamic lights with their shadows, weighted by a Henyey-Greenstein phase function. The grid is reprojected and blended with the previous frame, then integrated front to back; surfaces read the result as a 3D texture.",
  "sections": [
   {
    "title": "Froxel volumetric fog",
    "html": "<p>Replaces the old light-grid ray march with a camera-aligned 3D volume. Sunlight, baked light and dynamic lights illuminate fog spatially, shadows affect the medium, and the result is temporally accumulated before being integrated toward the camera.</p>\n<p class=\"req\"><strong>Requirements.</strong> Mode 2 requires the depth prepass (<code>r_depthPrepass 1</code>); otherwise the renderer falls back to the legacy fog path.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_volumetricFog 0 | 1 | 2</code> — off / legacy light-grid ray march / froxel volumetric fog. Requires <code>vid_restart</code>.</li><li><code>r_volumetricFogQuality 0..2</code> — low / medium / high. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_volumetricFogGridScale &lt;pixels&gt;</code> — manual screen pixels per froxel; <code>0</code> uses the preset. Requires <code>vid_restart</code>.</li><li><code>r_volumetricFogSlices &lt;count&gt;</code> — manual depth slices; <code>0</code> uses the preset. Requires <code>vid_restart</code>.</li><li><code>r_volumetricFogFar &lt;units&gt;</code> — froxel range; <code>0</code> uses the automatic default.</li><li><code>r_volumetricFogAnisotropy &lt;g&gt;</code> — fallback Henyey-Greenstein phase asymmetry.</li><li><code>r_volumetricFogTemporal 0 | 1</code> — temporal accumulation.</li><li><code>r_volumetricFogHistoryWeight &lt;value&gt;</code> — history weight.</li><li><code>r_volumetricFogSunScale &lt;value&gt;</code> — sun scattering multiplier.</li><li><code>r_volumetricFogDlightScale &lt;value&gt;</code> — dynamic-light scattering multiplier.</li><li><code>r_volumetricFogStaticScale &lt;value&gt;</code> — baked-light scattering multiplier.</li><li><code>r_volumetricFogDlightShadows 0 | 1</code> — dynamic-light shadows inside fog; needs <code>r_dlightMode 2</code>.</li><li><code>r_volumetricFogBloom &lt;value&gt;</code> — add bright in-scattering to bloom.</li><li><code>r_volumetricFogReset 1</code> — gameplay hook to clear temporal history after a camera cut.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 1..56</code> — density, lighting, history, media, particle, self-shadow, multiple-scattering and RGB-extinction views.</li><li><code>r_volumetricFogFreeze 0 | 1</code> — freeze the current volume and camera.</li><li><code>r_vfog</code> — inspect/control the active volumetric fog state.</li></ul>\n<p>Can significantly affect performance. Cost scales with froxel resolution, slice count, active lights and the optional effects below.</p>"
   },
   {
    "title": "Height fog",
    "html": "<p>Adds procedural ground haze to maps without requiring a BSP fog volume. Density is strongest around a base world height and falls exponentially upward.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog (<code>r_volumetricFog 2</code>).</p>\n<h4>Main controls</h4>\n<ul><li><code>r_volumetricFogHeight 0 | 1</code> — disable / enable height fog.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_volumetricFogHeightOpaqueDistance &lt;units&gt;</code> — opaque distance at the base height.</li><li><code>r_volumetricFogHeightBase &lt;z&gt;</code> — base world height.</li><li><code>r_volumetricFogHeightFalloff &lt;units&gt;</code> — exponential scale height.</li><li><code>r_volumetricFogHeightMaxDensity &lt;value&gt;</code> — density cap below the base.</li><li><code>r_volumetricFogHeightTopHeight &lt;units&gt;</code> — optional soft upper cutoff.</li><li><code>r_volumetricFogHeightColor \"r g b\"</code> — scattering color.</li></ul>\n<p>Adds little overhead beyond the froxel fog pass itself.</p>"
   },
   {
    "title": "Volumetric density noise",
    "html": "<p>Breaks uniform fog into world-space patches, clouds and drifting mist. A macro layer controls broad density variation and an optional detail layer adds smaller structure.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog (<code>r_volumetricFog 2</code>).</p>\n<h4>Main controls</h4>\n<ul><li><code>r_volumetricFogNoise &lt;bits&gt;</code> — 1 height fog, 2 BSP fog, 4 global fog, 8 noise-enabled local volumes; bits can be combined.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_volumetricFogNoiseScale &lt;units&gt;</code> — macro-noise period.</li><li><code>r_volumetricFogNoiseContrast &lt;value&gt;</code> — macro contrast.</li><li><code>r_volumetricFogNoiseDetailScale &lt;units&gt;</code> — detail-noise period.</li><li><code>r_volumetricFogNoiseDetailContrast &lt;value&gt;</code> — detail strength; <code>0</code> avoids the second fetch.</li><li><code>r_volumetricFogNoiseWind \"x y z\"</code> — world-space drift speed.</li></ul>\n<p>May slightly to moderately affect performance depending on how much of the froxel volume contains noisy media.</p>"
   },
   {
    "title": "Directional baked light in volumetrics",
    "html": "<p>Recovers where the baked (non-sun) light of the BSP light grid appears to come from, once at map load, so fog subtly brightens towards the lamps it is lit by instead of glowing uniformly. Red and blue lamps keep their own directions; where the light grid gives no confident answer the fog stays isotropic. It only redistributes the existing baked light over view directions, never adds light, and never changes the fog density. Stock maps need no changes; static emissive surfaces and <code>maps/&lt;map&gt;.arealights.json</code> lamps help it.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog (<code>r_volumetricFog 2</code>). Changing the mode needs <code>vid_restart</code>.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_vfogStaticStats [proxies]</code> — what the reconstruction found on the loaded map.</li></ul>\n<p>Mode 0 costs nothing. Modes 1 and 2 add three small 3D texture fetches per participating froxel and a short map load step (a fraction of a second on large maps).</p>"
   },
   {
    "title": "Rain suspension in froxel fog",
    "html": "<p>Adds rain haze as another participating medium of the existing froxel volume. It uses the resolved rain subtype and intensity, and samples the top-down weather depth for every froxel: outdoor courtyards receive haze while rooms and the air below roofs do not. Sun, baked and dynamic lights (including saber, flashlight-like spot lights and explosions), shadows, HG phase and temporal reprojection are the normal froxel paths; there is no separate rain lighting pass. Acid rain uses the opt-in coherent renderer profile described in <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-acid-rain-profile.md\" target=\"_blank\" rel=\"noopener\"><code>docs/rend2-acid-rain-profile.md</code></a>.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires active rain, its weather depth map and froxel fog (<code>r_volumetricFog 2</code>). The feature is off by default.</p>\n<ul><li><code>r_rainVolume 0 | 1</code> — disable / enable the rain medium.</li><li><code>r_rainVolumeStrength &lt;value&gt;</code> — normal-rain extinction per world unit at full normalized intensity.</li><li><code>r_rainVolumeHeavyMultiplier &lt;value&gt;</code> — additional heavy-rain density multiplier.</li><li><code>r_rainVolumeAnisotropy &lt;g&gt;</code> — rain medium HG anisotropy; default <code>0.72</code>.</li><li><code>r_rainVolumeGroundStrength &lt;value&gt;</code> — optional heavy-rain splash-mist extinction; <code>0</code> disables it.</li><li><code>r_rainVolumeGroundHeight &lt;units&gt;</code> — height of that layer above the exposed surface.</li><li><code>r_rainVolumeMaxDistance &lt;units&gt;</code> — camera range with a soft fade over the outer 15%.</li><li><code>r_volumetricFogDebug 71..74</code> — rain-only density, weather exposure, unattenuated in-scattering source and final integrated rain contribution.</li></ul>"
   }
  ],
  "media": []
 },
 "media": {
  "group": "Volumetrics and sky",
  "title": "Fog volumes and FX media",
  "cvar": "r_volumetricParticles 1",
  "how": "Fog volumes and opted-in effect particles are analytic shapes (spheres, ellipsoids, boxes) that are culled per depth slice and add their density, color and emission to the froxels they overlap. From there they are lit and shadowed exactly like the rest of the fog. Ordinary smoke sprites sample the same light field instead of using a fixed color.",
  "sections": [
   {
    "title": "Local fog volumes",
    "html": "<p><strong>For mod authors.</strong> Small analytic spheres, ellipsoids and boxes can add smoke pockets, steam clouds or local haze without changing the BSP.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog (<code>r_volumetricFog 2</code>). The easiest persistent authoring route is the existing environment file <code>cubemaps/&lt;map&gt;/env.json</code>.</p>\n<pre><code>{\n  &quot;FogVolumes&quot;: [\n    {\n      &quot;Shape&quot;: &quot;sphere&quot;,\n      &quot;Origin&quot;: [0, 0, 128],\n      &quot;Radius&quot;: 96,\n      &quot;Opaque&quot;: 250,\n      &quot;Color&quot;: [0.55, 0.55, 0.58],\n      &quot;Softness&quot;: 0.6\n    }\n  ]\n}</code></pre>\n<p>Useful keys include <code>Shape</code>, <code>Origin</code>, <code>Radius</code> or <code>Size</code>, <code>Angles</code>, <code>Opaque</code>, <code>Color</code>, <code>Softness</code> and <code>Noise</code>. A runtime <code>GetRefFogVolumeAPI</code> is also available for engine/mod code.</p>\n<h4>Debug / authoring tools</h4>\n<ul><li><code>r_fogvol</code> — print volume state and statistics.</li><li><code>r_fogvol add ...</code> — spawn a test volume.</li><li><code>r_fogvol test &lt;count&gt; [spread]</code> — deterministic stress test.</li><li><code>r_fogvol remove &lt;index&gt; | clear</code> — remove test volumes.</li><li><code>r_fogvol slices</code> — inspect per-slice lists.</li><li><code>r_fogvol dump</code> — print test volumes as an env.json <code>FogVolumes</code> array.</li></ul>\n<p>Cost depends on how many volumes overlap the visible froxel slices; isolated volumes are relatively inexpensive.</p>"
   },
   {
    "title": "Volumetric FX particles",
    "html": "<p><strong>For mod authors.</strong> Smoke, steam, dust and explosion-cloud particles can become real participating media instead of only transparent sprites. They then receive baked light, sun, dynamic lights, volumetric shadows and phase scattering naturally.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog (<code>r_volumetricFog 2</code>) and <code>r_volumetricParticles 1</code>. Nothing is detected automatically: a Particle or OrientedParticle must opt in.</p>\n<pre><code>Particle\n{\n    ...\n    volumetricMedia\n    {\n        extinction 0.04 0.06\n        albedo 0.07 0.07 0.07\n        radiusScale 1.0\n        aspect 1 1 0.8\n        softness 0.7\n    }\n}</code></pre>\n<h4>Main controls</h4>\n<ul><li><code>r_volumetricParticles 0 | 1</code> — disable / submit authored volumetric FX media.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_volumetricParticlesMax &lt;count&gt;</code> — maximum important particles uploaded per frame.</li><li><code>r_volumetricParticlesScale &lt;value&gt;</code> — extinction multiplier.</li><li><code>r_volumetricParticlesHistory &lt;value&gt;</code> — temporal-history retention where particle density changes.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricParticlesDebug 0 | 1</code> — print culling statistics periodically.</li><li><code>r_volumetricFogDebug 26..28</code> — particle density, temporal-history reduction and proxy bounds.</li></ul>\n<p><strong>Binary note.</strong> This is not renderer-only. SP adds a new cgame trap for volumetric particles and the engine forwards it to the renderer; MP submits them from the client FX system. Use the matching updated engine/client and game module.</p>\n<p>Can moderately or significantly affect performance when many large volumetric particles overlap the view.</p>"
   },
   {
    "title": "Volumetric emission",
    "html": "<p><strong>For mod authors.</strong> Fog volumes and volumetric particles can emit light themselves, enabling glowing gas, fireballs and luminous smoke. Emission is added to the HDR volumetric result and can naturally enter bloom.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog. EFX particle emission additionally uses the volumetric-particle path.</p>\n<p>For local <code>env.json</code> volumes, use <code>\"Emissive\": [r,g,b]</code> and optionally <code>\"EmissiveDensity\": d</code>. <code>\"Opaque\": 0</code> with an explicit emissive density creates a pure glowing volume.</p>\n<pre><code>volumetricMedia\n{\n    extinction      0.004 0.006\n    emissive        6 3 1.2\n    emissiveDensity 0.02\n    emissiveTint    1\n}</code></pre>\n<h4>Main controls</h4>\n<ul><li><code>r_volumetricEmission &lt;value&gt;</code> — global volumetric emission scale; <code>0</code> disables it.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 30..34</code> — scattering source, emissive source, combined source, integrated emission and history/emission separation.</li></ul>\n<p>Adds little overhead when no medium emits; active emitting media add extra injection work.</p>"
   },
   {
    "title": "Sprite particle lighting",
    "html": "<p>Lights ordinary alpha-blended smoke and dust sprites from the same local light field used by froxel fog, so a smoke sprite in a dark room is dark and one beside a saber or lamp receives that color. Additive sprites remain unlit.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog (<code>r_volumetricFog 2</code>).</p>\n<h4>Main controls</h4>\n<ul><li><code>r_particleLighting 0 | 1</code> — authored sprite color / volumetric-light-field lighting. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_particleLightingingMix 0..1</code> — blend between authored and lit color.</li><li><code>r_particleLightingingScale &lt;value&gt;</code> — light-field gain.</li><li><code>r_particleLightingingFloor &lt;value&gt;</code> — minimum light factor.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_particleLightingingDebug 1..5</code> — field, baked-only, sun-only, dynamic-only and sprite classification.</li></ul>\n<p>Moderate additional cost because the volumetric light field must exist and lit sprites sample it.</p>"
   }
  ],
  "media": []
 },
 "scattering": {
  "group": "Volumetrics and sky",
  "title": "Scattering in dense media",
  "cvar": "r_volumetricSelfShadow 1",
  "how": "Media density is first written into its own volume. For each lit froxel a short ray is marched toward the sun (and the strongest dynamic lights) through that volume to compute how much light the medium itself absorbs. Multiple scattering adds back a few weaker, wider octaves of that light, following Oz 2013. RGB extinction stores optical depth and transmittance per color channel.",
  "sections": [
   {
    "title": "Per-medium albedo and anisotropy",
    "html": "<p><strong>For mod authors.</strong> Different fog media can now have their own scattering color and phase anisotropy instead of sharing one global <code>g</code> value. Smoke, steam and atmospheric haze can therefore behave differently even when they overlap.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog.</p>\n<p>BSP fog shaders can opt in:</p>\n<pre><code>textures/test/medium_fog\n{\n    surfaceparm fog\n    fogParms ( 0.6 0.6 0.65 ) 800\n    fogAnisotropy 0.6\n    fogAlbedo 0.9 0.4 0.3\n}</code></pre>\n<p>Local fog volumes can use <code>\"Anisotropy\": 0.6</code> in env.json. EFX <code>volumetricMedia</code> can use <code>anisotropy 0.6</code>. Media without an override continue to use the global <code>r_volumetricFogAnisotropy</code> fallback.</p>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 35..39</code> — extinction, albedo, phase-lobe slots, mixed anisotropy and sun phase.</li></ul>\n<p>May slightly affect performance when several different media overlap.</p>"
   },
   {
    "title": "Volumetric self-shadowing",
    "html": "<p>Makes dense fog, smoke and clouds attenuate light <strong>inside the medium itself</strong>. The far side of a dense cloud can therefore be darker than the side facing the sun even when no solid geometry blocks it.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_volumetricSelfShadow 0 | 1 | 2</code> — off / sun / sun + strongest dynamic lights. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_volumetricSelfShadowSamples &lt;count&gt;</code> — density samples along the sun ray.</li><li><code>r_volumetricSelfShadowDistance &lt;units&gt;</code> — maximum light-ray march.</li><li><code>r_volumetricSelfShadowOutsideHeightFog 0 | 1</code> — ignore / analytically include height fog beyond the march.</li><li><code>r_volumetricSelfShadowMaxLights &lt;count&gt;</code> — strongest dynamic lights self-shadowed in mode 2.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 40..45</code> — density, optical depth, media transmittance and geometry/media shadow separation.</li></ul>\n<p>Can significantly affect performance because participating froxels trace additional rays through the media.</p>"
   },
   {
    "title": "Approximate multiple scattering",
    "html": "<p>Returns part of the light removed by volumetric self-shadowing in dense high-albedo media. It prevents white steam or cloud interiors from becoming unrealistically black while keeping absorptive smoke much darker.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires <code>r_volumetricSelfShadow 1</code> or <code>2</code>. Without a media shadow there is no lost light to recover.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_volumetricMultiScatter 0 | 1 | 2</code> — off / sun / sun + self-shadowed dynamic lights.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_volumetricMultiScatterOctaves 1..3</code> — scattering orders beyond single scattering.</li><li><code>r_volumetricMultiScatterAttenuation &lt;value&gt;</code> — optical-depth scale per octave.</li><li><code>r_volumetricMultiScatterContribution &lt;value&gt;</code> — returned energy per octave.</li><li><code>r_volumetricMultiScatterPhase &lt;value&gt;</code> — anisotropy reduction per octave.</li><li><code>r_volumetricMultiScatterLength &lt;units&gt;</code> — characteristic dense-medium size.</li><li><code>r_volumetricMultiScatterShadowFill &lt;value&gt;</code> — limited fill into geometry shadows.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 46..50</code> — single scattering, multiple scattering, combined result, ratio and optical depth.</li></ul>\n<p>Adds no extra pass or texture, but adds substantial math to self-shadowed froxels. The incremental cost is moderate and grows with octave count and dynamic-light mode.</p>"
   },
   {
    "title": "RGB extinction / wavelength-dependent absorption",
    "html": "<p><strong>For mod authors.</strong> Participating media can absorb red, green and blue light at different rates. This can model effects such as water losing red light with depth or colored smoke tinting transmitted backgrounds.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires froxel fog. <code>r_volumetricFogRGBExtinction 1</code> also needs enough texture units; unsupported hardware falls back to scalar extinction.</p>\n<p>BSP fog shaders can use:</p>\n<pre><code>fogExtinctionColor 3 0.5 0.5</code></pre>\n<p>Local env.json volumes can use <code>\"Extinction\": [r,g,b]</code>. Height fog uses <code>r_volumetricFogHeightExtinction \"r g b\"</code>. FX particle media currently remain neutral.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_volumetricFogRGBExtinction 0 | 1</code> — scalar / per-channel extinction and transmittance. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_volumetricFogHeightExtinction \"r g b\"</code> — relative RGB extinction for procedural height fog.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 51..56</code> — RGB extinction, transmittance, color shift, difference heat, chroma and tail transmittance.</li></ul>\n<p>This is one of the heavier volumetric options: it adds about 24 bytes per froxel, extra 3D buffers and a second composite draw.</p>"
   }
  ],
  "media": []
 },
 "atmosphere": {
  "group": "Volumetrics and sky",
  "title": "Atmosphere",
  "cvar": "r_atmosphere 1",
  "how": "Following Hillaire's model, small lookup textures are precomputed for transmittance and multiple scattering, and a sky-view texture is built each frame from the sun direction. The composite pass reads depth, computes the air path from the camera to each surface and applies transmittance and in-scattering analytically, before the froxel fog is composited.",
  "sections": [
   {
    "title": "Atmosphere",
    "html": "<p>Adds the air between the camera and distant surfaces, and optionally the sky itself. Distant terrain fades into haze and the sky gets a physically based color around the sun. The froxel fog keeps the local media; the atmosphere covers the long range beyond it.</p>\n<p class=\"req\"><strong>Requirements.</strong> Works with or without froxel fog. A map sun direction is needed for the sun glow.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_atmosphere 0 | 1</code> — off / on.</li><li><code>r_atmosphereSky 0 | 1 | 2</code> — overlay on the skybox / blend with it / analytic sky.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_atmosphereSkyBlend &lt;value&gt;</code> — share of the analytic sky in mode 1.</li><li><code>r_atmosphereUnitScale &lt;value&gt;</code> — metres per world unit.</li><li><code>r_atmosphereAerialScale &lt;value&gt;</code> — aerial path length multiplier (8–30 for visible haze).</li><li><code>r_atmosphereAltitude &lt;metres&gt;</code> — ground altitude above sea level.</li><li><code>r_atmosphereGroundZ &lt;z&gt;</code> — world height of the ground; automatic by default.</li><li><code>r_atmosphereRayleigh</code>, <code>r_atmosphereMie</code>, <code>r_atmosphereOzone &lt;value&gt;</code> — density multipliers.</li><li><code>r_atmosphereMieG &lt;g&gt;</code> — haze anisotropy.</li><li><code>r_atmosphereSunColor 0 | 1 | 2</code> — map sun at the top of the atmosphere / at the ground / white.</li><li><code>r_atmosphereSunIntensity &lt;value&gt;</code>, <code>r_atmosphereSunSize &lt;degrees&gt;</code>, <code>r_atmosphereSunGlow &lt;value&gt;</code> — sun.</li><li><code>r_atmosphereStart &lt;units&gt;</code> — distance before which there is no aerial perspective.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_atmosphereDebug</code> — debug views.</li><li><code>r_atmosphereInfo</code> — print the current state.</li></ul>"
   }
  ],
  "media": []
 },
 "clouds": {
  "group": "Volumetrics and sky",
  "title": "Clouds",
  "cvar": "r_clouds 1",
  "how": "Clouds live in a spherical shell between two altitudes. The shell is ray marched only on sky pixels, at reduced resolution, with a light march toward the sun at each sample and an approximation of multiple scattering; the result is upscaled with a temporal history of its own. Cloud shadows are a top-down transmittance map that surfaces and fog sample.",
  "sections": [
   {
    "title": "Clouds",
    "html": "<p>Draws volumetric clouds in a layer high above the map, lit by the sun, moving with the wind, and optionally casting their shadows on the ground and into the fog. All noise is generated by the renderer; no assets are needed.</p>\n<p class=\"req\"><strong>Requirements.</strong> Needs an outdoor map with a sky. Works best together with <code>r_atmosphere 1</code>.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_clouds 0 | 1</code> — off / on. Requires <code>vid_restart</code>.</li><li><code>r_cloudShadows 0 | 1</code> — cloud shadows on surfaces and fog.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_cloudScale 1 | 2 | 4</code> — march resolution divisor. Requires <code>vid_restart</code>.</li><li><code>r_cloudBase</code>, <code>r_cloudTop &lt;metres&gt;</code> — layer altitude above the ground.</li><li><code>r_cloudCoverage 0..1</code> — clear / weather map / overcast.</li><li><code>r_cloudDensity &lt;value&gt;</code> — extinction multiplier.</li><li><code>r_cloudAnisotropy &lt;g&gt;</code> — forward scattering lobe.</li><li><code>r_cloudWindSpeed &lt;m/s&gt;</code>, <code>r_cloudWindDir &lt;degrees&gt;</code> — wind.</li><li><code>r_cloudSteps &lt;count&gt;</code>, <code>r_cloudStepLength &lt;metres&gt;</code> — ray march.</li><li><code>r_cloudShadowSteps &lt;count&gt;</code> — light march taps.</li><li><code>r_cloudMSOctaves &lt;count&gt;</code> — multiple scattering octaves.</li><li><code>r_cloudDetail &lt;value&gt;</code> — erosion strength; <code>0</code> disables it.</li><li><code>r_cloudAmbient &lt;value&gt;</code> — sky ambient.</li><li><code>r_cloudTemporal &lt;value&gt;</code> — history weight; <code>0</code> disables it.</li><li><code>r_cloudMaxDistance &lt;km&gt;</code> — draw distance.</li><li><code>r_cloudShapeScale</code>, <code>r_cloudDetailScale</code>, <code>r_cloudWeatherScale &lt;km&gt;</code> — noise tile sizes.</li><li><code>r_cloudLegacy 0 | 1 | 2</code> — use of the map's legacy cloud layer.</li><li><code>r_cloudSkyPortal 0 | 1</code> — clouds on sky portal maps.</li><li><code>r_cloudShadowExtent &lt;km&gt;</code>, <code>r_cloudShadowInterval &lt;frames&gt;</code> — shadow map size and update rate.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_cloudDebug</code> — debug views.</li></ul>\n<p>For lower cost use <code>r_cloudScale 4</code>, <code>r_cloudSteps 32</code> and <code>r_cloudDetail 0</code>.</p>"
   }
  ],
  "media": []
 },
 "water": {
  "group": "Water and weather",
  "title": "Water surface",
  "cvar": "r_waterSurface 1",
  "how": "The surface normal comes from a deterministic sum of directional waves plus a tiled slope texture filtered so that unresolved waves raise roughness instead of aliasing; near the camera the mesh is tessellated. Reflection uses water SSR, then the cubemap. Refraction offsets the scene behind the surface, rejects foreground objects by depth, and attenuates color along the refracted path through the liquid.",
  "sections": [
   {
    "title": "Water surface",
    "html": "<p>Replaces the stock water of the maps with a physically based surface: Fresnel reflection, depth-aware refraction, absorption and scattering in the liquid, sun and light glints. Water is found from the map's BSP data; no map is rebuilt.</p>\n<p class=\"req\"><strong>Requirements.</strong> HDR is recommended.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_waterSurface 0 | 1</code> — stock / modern water. Requires <code>vid_restart</code>.</li><li><code>r_waterSnell 0 | 1</code> — Snell's window and total internal reflection when looking up from under water.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_waterSurfaceIOR &lt;value&gt;</code> — index of refraction.</li><li><code>r_waterSurfaceRoughness &lt;value&gt;</code> — base roughness; unresolved waves add to it.</li><li><code>r_waterSurfaceNormal &lt;value&gt;</code> — wave normal strength.</li><li><code>r_waterWind 0 | 1</code>, <code>r_waterWindScale &lt;value&gt;</code>, <code>r_waterWindTransition &lt;seconds&gt;</code> — wave response to wind and weather.</li><li><code>r_waterSurfaceRefraction &lt;value&gt;</code> — refraction offset scale.</li><li><code>r_waterSurfaceDepthReject 0 | 1</code> — reject foreground objects from the refraction.</li><li><code>r_waterSurfaceReflection &lt;value&gt;</code> — reflection scale.</li><li><code>r_waterSurfaceSSR &lt;value&gt;</code> — weight of water reflections over the cubemap.</li><li><code>r_waterSurfaceAbsorption &lt;value&gt;</code>, <code>r_waterSurfaceDepthScale &lt;value&gt;</code> — absorption and path length.</li><li><code>r_waterSnellOptics 0 | 1 | 2</code> — sun transport through the surface; 2 also shows the atmosphere through Snell's window.</li><li><code>r_waterSurfaceExperimental 0 | 1</code> — name-based water detection.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterSurfaceDebug</code>, <code>r_waterSnellDebug</code>, <code>r_waterWindDebug</code> — debug views.</li><li><code>r_waterInfo [surfaces]</code> — print liquid brushes and water shader decisions.</li></ul>"
   }
  ],
  "media": []
 },
 "waterparticles": {
  "group": "Water and weather",
  "title": "Underwater particles",
  "cvar": "r_waterParticles 1",
  "how": "Particles are simulated on the CPU in a sphere around the camera and only spawn where they are inside the water body and visible. They are drawn as small billboards lit from the light grid, after the other transparent surfaces, and fogged by the water medium like any other transparent draw.",
  "sections": [
   {
    "title": "Underwater particles",
    "html": "<p>Adds a few dozen to a few hundred tiny specks and small bubbles around the camera while it is under water. Near specks read as particles, far ones fade into the water color, which gives a sense of depth.</p>\n<p class=\"req\"><strong>Requirements.</strong> The camera must be inside a water or slime volume.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_waterParticles 0 | 1</code> — off / on.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_waterParticlesDensity 0..8</code> — density scale.</li><li><code>r_waterParticlesSize 0.25..4</code> — size scale.</li><li><code>r_waterParticlesSpeed 0..4</code> — drift and rise speed.</li><li><code>r_waterParticlesBubbles 0..1</code> — share of bubbles; <code>-1</code> uses the water profile.</li><li><code>r_waterParticlesDistance &lt;units&gt;</code> — radius around the camera.</li><li><code>r_waterParticlesProfile -1..3</code> — auto / clean / murky / dirty water.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterParticlesDebug 1..3</code> — placement, no medium in front, statistics.</li></ul>\n<p>With <code>r_waterParticles 0</code> nothing is simulated or drawn.</p>"
   }
  ],
  "media": []
 },
 "rain": {
  "group": "Water and weather",
  "title": "Rain",
  "cvar": "r_rainStreaks 1",
  "how": "Drops are simulated on the GPU. Each one is drawn as a premultiplied streak stretched along its velocity and lit from the light grid, the sun and nearby lights. A splash is created in the frame when a drop crosses the top-down weather depth map, the first rain-exposed surface below it, without CPU traces or readbacks. Far away, streaks hand over to rain haze in the froxel fog.",
  "sections": [
   {
    "title": "Modern rain streaks",
    "html": "<p>Replaces simple additive rain particles with lit, premultiplied streaks whose length follows velocity and wind. The streaks also fade near surfaces and at very small screen sizes.</p>\n<p class=\"req\"><strong>Requirements.</strong> Visible only while the map's rain weather system is active.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_rainStreaks 0 | 1</code> — legacy / modern rain streaks.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_rainStreakWidth &lt;value&gt;</code> — streak width.</li><li><code>r_rainStreakLength &lt;value&gt;</code> — length multiplier.</li><li><code>r_rainStreakOpacity &lt;value&gt;</code> — opacity.</li><li><code>r_rainStreakLighting 0 | 1</code> — fixed brightness / light-grid + sun lighting.</li><li><code>r_rainDynamicLighting 0 | 1 | 2</code> — current grid/sun only / strongest nearby dynamic light / up to the limited nearby Forward+ cluster lights. This also lights impact splashes and keeps <code>r_rainStreakLighting</code>'s existing meaning.</li><li><code>r_rainDynamicMaxLights 1..8</code> — accepted dynamic lights per streak or splash in mode 2 (default 4). At most eight importance-ordered cluster/UBO candidates are examined; finite-radius attenuation and a contribution cutoff reject distant or negligible lights.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_rainStreakDebug 1..5</code> — coverage, distance, lighting, contact and variation views.</li><li><code>r_rainDynamicDebug 1 | 2 | 3</code> — show the static/grid contribution, dynamic contribution, or normalized selected-light count.</li></ul>\n<p>Forward+ point/spot lights, saber line lights, blaster lights and explosion lights are read directly from the current renderer light data; rain has no separate light submission. Without an available cluster list, the same bounded loop reads the <code>Lights</code> UBO. Mode 0 returns the previous static result before evaluating any light. Use <code>r_speeds 100</code> and compare the existing <strong>Weather draw</strong> and <strong>Weather splashes</strong> GPU timers with mode 0/1/2 on both a light-rain and a heavy-rain map.</p>\n<p>Mode 1 is intended for heavy rain. Mode 2 costs more per visible streak/splash but remains bounded independently of total scene-light count.</p>"
   },
   {
    "title": "Rain streak distance LOD",
    "html": "<p>Hands distant rain from real streak geometry to the weather-occluded froxel medium. The stock distribution is nine 2000-unit square chunks around the camera: light, normal and heavy rain simulate 9,000, 18,000 and 45,000 drops respectively. Before this LOD, every visible chunk submitted its full count and streaks faded from 900 to 6000 units.</p>\n<p>The near band keeps every streak. Across the middle band a stable per-particle hash reduces density to the configured floor and retained streaks receive inverse-density opacity compensation, capped at 4x to avoid saturated, aliased lines. Across that same wide band the streak contribution crossfades out while the rain medium reaches full weight. Beyond the middle distance no streak geometry is emitted.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires an actually active rain volume (<code>r_rainVolume 1</code>, <code>r_volumetricFog 2</code>, weather depth and supported texture-unit budget). If the volume cannot render, LOD automatically falls back to all existing streaks so distant rain never disappears.</p>\n<ul><li><code>r_rainLOD 0 | 1</code> — master switch; default <code>1</code>.</li><li><code>r_rainLODNear &lt;units&gt;</code> — full-density radius; default <code>1200</code>.</li><li><code>r_rainLODMid &lt;units&gt;</code> — end of the streak/volume handoff; default <code>3600</code>.</li><li><code>r_rainLODDensityFloor 0.05..1</code> — lowest stochastic density in the middle band; default <code>0.25</code>.</li><li><code>r_rainLODFarVolumeCoupling 0..1</code> — <code>0</code> keeps the original all-distance medium, <code>1</code> fades the medium in across the LOD handoff.</li><li><code>r_rainLODDebug 0 | 1</code> — show green near, yellow-to-blue middle and blue far bands. Debug deliberately restores far streak markers.</li><li><code>r_speeds 100</code> — prints the current LOD/fallback state and the existing weather submission counts.</li></ul>"
   },
   {
    "title": "Rain impact splashes",
    "html": "<p>Creates short splash rings/spray where GPU rain drops cross the weather depth surface. No CPU trace is performed for each drop.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires active rain and the weather depth map.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_rainSplashes 0 | 1</code> — disable / enable impact splashes.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_rainSplashSize &lt;units&gt;</code> — splash radius.</li><li><code>r_rainSplashLifetime &lt;ms&gt;</code> — lifetime.</li><li><code>r_rainSplashOpacity &lt;value&gt;</code> — opacity.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_rainSplashDebug 1..3</code> — impact points, accepted/rejected crossings and recent trajectories.</li></ul>\n<p>Usually moderate only in very heavy rain; normal rain has a much smaller cost.</p>"
   },
   {
    "title": "Weather chunk culling",
    "html": "<p>Skips rain/snow weather chunks that are outside the camera frustum. It is intended to produce exactly the same image while avoiding unnecessary weather work.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_weatherCull 0 | 1</code> — draw every weather chunk / cull invisible chunks.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherDebugChunks 1 | 2</code> — visualize chunk bounds; mode 2 also prints detailed bounds.</li></ul>\n<p>No visual cost and usually improves performance.</p>"
   }
  ],
  "media": []
 },
 "wetness": {
  "group": "Water and weather",
  "title": "Wet surfaces",
  "cvar": "r_weatherWetness 1",
  "how": "Rain exposure is read per pixel from the same top-down weather depth map that occludes rain, so roofs keep the ground below them dry. Exposed surfaces get darker albedo, lower roughness and flatter normals by material class. Puddles, ripples and runoff are procedural and only change the normal and roughness, so direct light, cubemaps and SSR all react to them.",
  "sections": [
   {
    "title": "Wet surfaces",
    "html": "<p>Makes rain-exposed PBR surfaces darker, smoother and more reflective while roofs and other occluders remain dry. The response is material-aware: cloth mainly darkens, while armor and metal can become substantially glossier.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires an active rain map. Uses the weather occlusion depth map.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_weatherWetness 0 | 1</code> — disable / enable wet materials. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_weatherWetnessStrength 0..1</code> — overall wetness.</li><li><code>r_weatherWetnessRoughness &lt;value&gt;</code> — scale of class-specific smoothing.</li><li><code>r_weatherWetnessDarkening &lt;value&gt;</code> — scale of class-specific albedo darkening.</li><li><code>r_weatherWetnessNormal &lt;value&gt;</code> — scale of class-specific normal flattening.</li><li><code>r_weatherWetnessEntityFacing &lt;value&gt;</code> — wetness of vertical character/prop faces.</li><li><code>r_weatherWetnessBias &lt;units&gt;</code> — rain-occlusion depth bias.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 1..4</code> — exposure, wetness, roughness and dry/wet split.</li><li><code>r_weatherSurfaceDebug 16</code> — detected wet material class.</li><li><code>r_weatherSurfaceDebug 21</code> — geometric normal.</li></ul>\n<p>May slightly affect performance on rain-exposed PBR surfaces.</p>"
   },
   {
    "title": "Procedural puddles",
    "html": "<p>Creates standing water on flat rain-exposed world surfaces without editing the BSP. World-space noise breaks the water into irregular patches, and the resulting low roughness is automatically visible in direct light, cubemaps and SSR.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires <code>r_weatherWetness 1</code> and active rain.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_weatherPuddles 0 | 1</code> — disable / enable procedural puddles.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_weatherPuddleCoverage 0..1</code> — approximate covered fraction.</li><li><code>r_weatherPuddleRoughness &lt;value&gt;</code> — water roughness.</li><li><code>r_weatherPuddleSlope \"min max\"</code> — geometric-normal range where puddles can form.</li><li><code>r_weatherPuddleScale &lt;units&gt;</code> — macro puddle pattern size.</li></ul>\n<h4>Layered edge response (opt-in)</h4>\n<ul><li><code>r_weatherPuddleEdgeModel 0 | 1</code> — legacy response / continuous dry, damp fringe, thin film and standing-water response. Default <code>0</code> preserves the existing puddle shader.</li><li><code>r_weatherPuddleFringeDarkening 0..1</code> — additional albedo darkening at the damp fringe (default <code>0.18</code>).</li><li><code>r_weatherPuddleFilmRoughness 0.02..1</code> — target roughness of the thin film (default <code>0.18</code>); standing water continues to use <code>r_weatherPuddleRoughness</code>.</li></ul>\n<p>This model only splits the existing <code>puddleEdge</code> and <code>puddle</code> masks; it does not create or expand puddle placement. The damp fringe keeps the underlying normal and receives only a mild roughness reduction. The thin film progressively smooths roughness and partially flattens the normal, allowing the existing dielectric Fresnel, cubemap and SSR response to appear. The standing-water core reaches the geometric macro normal and retains rain-ripple perturbation. Existing per-material <code>weatherResponse</code> scaling still gates the whole response.</p>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 5..10</code> — slope, noise, exposure, mask, roughness and eligibility.</li><li><code>r_weatherSurfaceDebug 13..15</code> — macro, micro and combined puddle masks.</li><li><code>r_weatherSurfaceDebug 33</code> — four continuous region weights: dry grey, damp brown, thin film cyan, standing water blue.</li><li><code>r_weatherSurfaceDebug 34..36</code> — final roughness, added puddle darkening and final normal for the layered edge model.</li></ul>\n<p>May slightly affect performance.</p>"
   },
   {
    "title": "Height-aware puddles",
    "html": "<p>Uses POM/material height data so water fills cracks and low regions before raised relief instead of simply applying one flat glossy mask.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires procedural puddles and a material with height data.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_weatherPuddleUseHeightMap 0 | 1</code> — ignore / use material height data.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_weatherPuddleHeightSoftness &lt;value&gt;</code> — water-line transition width.</li><li><code>r_weatherPuddleWaterLevelBias &lt;value&gt;</code> — bias the virtual water level.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 11..12</code> — material height and low/high regions.</li><li><code>r_weatherSurfaceDebug 15</code> — final combined puddle mask.</li></ul>\n<p>Adds little cost beyond puddles.</p>"
   },
   {
    "title": "Puddle ripples",
    "html": "<p>Adds procedural expanding rain rings to standing water. The rings perturb the puddle normal, so SSR, cubemap reflections and direct highlights all react to them.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires procedural puddles and active rain.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_weatherPuddleRipples 0 | 1</code> — disable / enable ripples.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_weatherPuddleRippleStrength &lt;value&gt;</code> — ring normal strength.</li><li><code>r_weatherPuddleRippleScale &lt;units&gt;</code> — cell/ring scale.</li><li><code>r_weatherPuddleRippleRate &lt;value&gt;</code> — ring cycles per second.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 17..20</code> — ripple height, normal offset, masking and final normal.</li></ul>\n<p>May slightly affect performance.</p>"
   },
   {
    "title": "Water runoff",
    "html": "<p>Adds a thin animated water film and streaks on rain-exposed slopes and walls. Flow follows gravity projected onto the surface, while puddles remain the preferred response on nearly horizontal faces.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires <code>r_weatherWetness 1</code> and active rain.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_weatherRunoff 0 | 1</code> — disable / enable runoff.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_weatherRunoffStrength &lt;value&gt;</code> — film/streak strength.</li><li><code>r_weatherRunoffSpeed &lt;units/s&gt;</code> — flow speed.</li><li><code>r_weatherRunoffScale &lt;units&gt;</code> — streak-cell size.</li><li><code>r_weatherRunoffProbe &lt;value&gt;</code> — small outward rain-exposure probe for steep walls; <code>0</code> disables it.</li><li><code>r_weatherRunoffEntities 0 | 1</code> — also apply runoff to characters, props and movers.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 22..26</code> — slope class, projected gravity, runoff mask, animated flow and final roughness.</li></ul>\n<p>May slightly affect performance. It is shading-only and has no fluid simulation or additional pass.</p>"
   },
   {
    "title": "Per-material weather response",
    "html": "<p><strong>For mod authors.</strong> Automatic rain response can be overridden for exceptional surfaces: a roof can get wet without holding puddles, cloth can avoid standing water, or an indoor decorative shader can stay completely dry.</p>\n<p class=\"req\"><strong>Requirements.</strong> The corresponding wetness, puddle or runoff feature still needs to be enabled globally.</p>\n<pre><code>textures/example/floor_tiles\n{\n    {\n        map textures/example/floor_tiles\n        weatherResponse 1 0\n    }\n}\n\ntextures/example/roof_slope\n{\n    weatherResponse 1.3 1 0.3\n}</code></pre>\n<p>Syntax:</p>\n<pre><code>weatherResponse &lt;wetness&gt; [&lt;puddle&gt; [&lt;runoff&gt;]]</code></pre>\n<p>Values are 0..4; 1 is the automatic response and 0 disables that channel. A single <code>weatherResponse 0</code> disables all weather response.</p>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 27..31</code> — enabled/excluded state, wetness/puddle/runoff scales and exclusion reason.</li><li><code>r_weatherMaterialList</code> — print drawn shaders with non-default response or automatic exclusion for one frame.</li></ul>\n<p>No meaningful performance cost.</p>"
   }
  ],
  "media": []
 },
 "lenswater": {
  "group": "Water and weather",
  "title": "Lens water",
  "cvar": "r_rainLens 1",
  "how": "A small CPU simulation tracks drops that pin, merge and slide, plus a wetness film and trails. It is rasterized into a lens field that the composite pass uses to refract and defocus the scene, with reflections from the nearest cubemap and glints from the sun and lights.",
  "sections": [
   {
    "title": "Rain on the camera lens",
    "html": "<p>Adds persistent water on the camera lens:</p>\n<ul><li>Beads that pin, merge and depin, sliding drops that leave translucent wet trails and residual beads, and rivulets and sheets in heavy rain.</li><li>A burst of water when leaving a pool.</li></ul>\n<p>Rain only adds water while the camera is outside. Under cover, existing water keeps moving and dries. Light, normal, heavy and acid rain use different profiles, so heavy rain means turnover, film and rivulets rather than just more drops. See <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-rain-lens.md\" target=\"_blank\" rel=\"noopener\"><code>docs/rend2-rain-lens.md</code></a>.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires HDR (<code>r_hdr 1</code>) and the main world view. New rain needs active rain weather and an outside camera position.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_rainLens 0 | 1</code> — disable / enable lens water. Requires <code>vid_restart</code>.</li><li><code>r_rainLensQuality 0..2</code> — field resolution, drop count and defocus samples. Requires <code>vid_restart</code>.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_rainLensDensity 0..2</code> — rain input multiplier (default 1).</li><li><code>r_rainLensRefraction 0..4</code> — refraction strength.</li><li><code>r_rainLensDropSize 0.25..4</code> — drop size (geometry only).</li><li><code>r_rainLensFilm 0..2</code> — thin film / trail visibility.</li><li><code>r_rainLensBlur 0..2</code> — drop defocus.</li><li><code>r_rainLensReflection 0..2</code> — reflection of the environment (nearest cubemap) with Fresnel, and glints of the sun and nearby lights; 0 = refraction only.</li><li><code>r_rainLensInertia 0..2</code> — water lags behind strong camera acceleration (default 0).</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_rainLensDebug 1..9</code> — optical weight / blur, normal, UV offset, scene/final split, agents, film/wetness, pinning, transient state and controller panel; forces the pass on.</li><li><code>rainlens_event splash|spray|emerge</code>, <code>rainlens_profile light|rain|heavy|acid|auto</code>, <code>rainlens_clear</code>, <code>rainlens_stats</code>.</li><li>Tuning cvars (cheat): <code>r_rainLensFieldHeight</code>, <code>r_rainLensAgentLimit</code>, <code>r_rainLensPinning</code>, <code>r_rainLensMerge</code>, <code>r_rainLensFilmDecay</code>, <code>r_rainLensWetDecay</code>, <code>r_rainLensHeavyFlow</code>, <code>r_rainLensPeripheralBias</code>; profiling: <code>r_rainLensPBO</code>, <code>r_rainLensMipBlur</code>.</li></ul>\n<p><strong>Maps and effects.</strong> Waterfalls and spray: <code>LensWaterEmitters</code> in <code>cubemaps/&lt;map&gt;/env.json</code> or the efx <code>lensWater</code> primitive. Splashes of characters entering water nearby and leaving water yourself are automatic with the updated game modules. Scripts can use <code>r_we lenswater splash 1</code>.</p>\n<p>The CPU simulation is bounded: up to 128 drops, 256 micro drops and 8 sheets, about 0.2 ms at worst. The field is quality bounded, and the full-resolution composite only samples extra where there is water. A dry lens under cover costs nothing.</p>"
   }
  ],
  "media": []
 },
 "grass": {
  "group": "Foliage and loading",
  "title": "Grass cards",
  "cvar": "r_grassCardMode 3",
  "how": "Stock surface-sprite grass is rebuilt at load into world-fixed crossed cards packed into static vertex buffers, instead of billboards that turn with the camera. In adaptive mode the third card fades out with distance.",
  "sections": [
   {
    "title": "Grass card geometry",
    "html": "<p>Turns camera-facing grass billboards into fixed world-space crossed cards, reducing the “rotates with the camera” look and preventing blades from disappearing edge-on. It is especially visible on Yavin grass.</p>\n<p class=\"req\"><strong>Requirements.</strong> Applies to existing surface-sprite grass.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_grassCardMode 0 | 1 | 2 | 3</code> — legacy / two-card cross / three-card tuft / adaptive three-to-two cards.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_grassCardLodDist &lt;units&gt;</code> — third-card fade distance in adaptive mode.</li><li><code>r_grassCardWidth &lt;value&gt;</code> — card width multiplier.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_grassCardDebug 1..6</code> — card ID/direction, forced card counts and LOD visualization.</li></ul>\n<p>Moderate cost in dense grass because two or three cards increase geometry and alpha overdraw.</p>"
   },
   {
    "title": "Automatic foliage classification",
    "html": "<p>Recognizes likely leaf and plant surfaces in stock models so later foliage features can target vegetation without changing assets.</p>\n<p class=\"req\"><strong>Requirements.</strong> No visual dependency; this is classification used by leaf flutter, plant wind and interaction.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_autoFoliage 0 | 1 | 2</code> — off / conservative stock-safe detection / broader experimental detection.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_autoFoliageDebug 1 | 2</code> — classified surfaces / classified plus uncertain candidates.</li><li><code>r_autoFoliageList [filter]</code> — print detected foliage.</li></ul>\n<p>No measurable frame-time cost.</p>"
   }
  ],
  "media": []
 },
 "wind": {
  "group": "Foliage and loading",
  "title": "Wind",
  "cvar": "r_foliageWind 1",
  "how": "Grass offsets come from a world-space breeze field, so neighbouring plants move together in broad gusts with smaller local motion on top. Leaf flutter adds a small high-frequency displacement and normal wobble only to model surfaces classified as leaves; trunks and branches stay still.",
  "sections": [
   {
    "title": "Coherent foliage wind",
    "html": "<p>Replaces synchronized circular grass sway with a world-space breeze field. Nearby plants share broad gusts while retaining smaller local motion.</p>\n<p class=\"req\"><strong>Requirements.</strong> Applies to surface-sprite vegetation that already supports wind.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_foliageWind 0 | 1</code> — legacy sway / coherent breeze.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_foliageWindStrength &lt;value&gt;</code> — global strength.</li><li><code>r_foliageWindSpeed &lt;value&gt;</code> — time scale.</li><li><code>r_foliageWindDirection &lt;degrees&gt;</code> — world-space yaw.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_foliageWindDebug 1..4</code> — exaggerated motion, bend color, frozen time and large-gust visualization.</li></ul>\n<p>May slightly affect performance; work is vertex-side only.</p>"
   },
   {
    "title": "Leaf flutter",
    "html": "<p>Adds small high-frequency rustling to surfaces classified as leaves while leaving trunks and branches still. Stock Yavin leaf/vine cards are the main intended use.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires <code>r_autoFoliage 1</code> or <code>2</code>.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_leafFlutter 0 | 1</code> — disable / enable leaf flutter.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_leafFlutterStrength &lt;value&gt;</code> — position amplitude.</li><li><code>r_leafFlutterSpeed &lt;value&gt;</code> — animation speed.</li><li><code>r_leafFlutterNormal &lt;value&gt;</code> — lighting-normal wobble.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_leafFlutterDebug &lt;bits&gt;</code> — exaggerate, freeze, highlight fluttering surfaces and visualize displacement.</li></ul>\n<p>May slightly affect performance; it is small vertex work.</p>"
   }
  ],
  "media": []
 },
 "interaction": {
  "group": "Foliage and loading",
  "title": "Interaction",
  "cvar": "r_foliageInteraction 1",
  "how": "The game passes a few vertical capsules per character to the renderer. Grass reads a small state texture around the player that stores contact and recovery, and curves from the root. Fern and bush fronds are chains of rigid segments solved on the CPU for nearby plants, so a frond swings around its base as a whole instead of stretching.",
  "sections": [
   {
    "title": "Plant fronds (wind and contact)",
    "html": "<p>Fern, bush and grass-card models bend frond by frond. Each frond (found automatically from the model) is a chain of rigid segments that keeps its length: stiff at the base, soft at the tip, and much stiffer across the leaf than out of its plane, so a frond pushed sideways swings around its base instead of stretching. Wind bends every frond, and a character pushes a frond to one side as a whole (it never wraps around a leg). The fronds swing back with one small overshoot after a character leaves. The simulation runs on the CPU for the nearby plants, and every pass (shadows, fog, motion vectors) draws the same pose.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires automatic foliage classification. Wind direction and speed come from the foliage wind controls; contact needs <code>r_foliageInteraction 1</code>.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_plantWind 0 | 1</code> — disable / enable the breeze on the fronds.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_plantWindStrength 0..4</code> — breeze strength.</li><li><code>r_plantStiffness &lt;value&gt;</code> — frond stiffness (1 = default).</li><li><code>r_plantSimDistance &lt;units&gt;</code> / <code>r_plantWindDistance &lt;units&gt;</code> — full simulation / coarse simulation / at rest.</li><li><code>r_plantSimMaxPlants &lt;count&gt;</code> — plants simulated per frame.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_plantDebug &lt;bits&gt;</code> — frond centerlines, segment or bone colors, contact sides, CPU cost.</li><li><code>r_plantSimInfo</code> — print the simulated plants; <code>r_autoFoliageList</code> also prints the frond skeletons.</li></ul>\n<p>Small CPU cost for the plants near the camera; the skinning is cheap vertex work.</p>"
   },
   {
    "title": "Character / foliage interaction",
    "html": "<p>Characters push grass and ferns away as they move through them. The renderer receives a few vertical body capsules. Only what a body actually touches moves: grass at the legs is pushed exactly out of the body, ferns are pushed frond by frond (see Plant fronds). Grass blades curve from the root (a stiff base, a soft tip) instead of tilting as flat cards, spring back after the character left, and keep a faint trampled trail for a few seconds.</p>\n<p class=\"req\"><strong>Requirements.</strong> Fern interaction requires automatic foliage classification; grass interaction works on supported grass surfaces.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_foliageInteraction 0 | 1</code> — disable / enable character interaction.</li></ul>\n<h4>Other controls</h4>\n<ul><li><code>r_foliageInteractionStrength &lt;value&gt;</code> — contact depth scale.</li><li><code>r_foliageInteractionRadius &lt;value&gt;</code> — character capsule radius scale.</li><li><code>r_foliageInteractionMaxInteractors &lt;count&gt;</code> — uploaded character limit.</li><li><code>r_foliageInteractionNPCs 0 | 1</code> — player only / player + nearby NPCs and players.</li><li><code>r_grassTrample 0..1</code> — how much of the bend trampled grass keeps; <code>r_grassTrampleRecovery &lt;seconds&gt;</code> — its recovery.</li><li><code>r_grassBendSegments 1..8</code> — segments of a near grass sprite (1 = flat cards); <code>r_grassBendLodDist &lt;units&gt;</code> — how far the curved strips are used.</li><li><code>r_foliageBendFieldSize 64 | 128 | 256</code> — grass state texture size. Requires <code>vid_restart</code>.</li><li><code>r_foliageBendFieldExtent &lt;units&gt;</code> — world size of the grass state around the player.</li><li><code>r_foliageBendFieldStrength &lt;value&gt;</code> — grass displacement scale.</li><li><code>r_foliageBendFieldRecoveryTime &lt;seconds&gt;</code> / <code>r_foliageBendFieldDamping &lt;value&gt;</code> / <code>r_foliageBendFieldImpulse &lt;value&gt;</code> — grass spring back.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_foliageInteractionDebug &lt;bits&gt;</code> — capsules, exaggerated interaction, interaction-only view, grass contact heat, freeze and player-only modes.</li><li><code>r_foliageBendFieldDebug &lt;bits&gt;</code> — grass state overlay, covered square, freeze, exaggeration.</li><li><code>r_foliageBendFieldClear</code> — clear the grass state.</li><li><code>r_foliageInteractionList</code> — print submitted/uploaded interactors and the grass state.</li></ul>\n<p><strong>Binary note.</strong> This is not renderer-only: cgame supplies character capsules through a new optional renderer interface. SP needs the updated <code>openjk_sp</code> executable and game/cgame module; MP needs the matching updated client/cgame path.</p>\n<p>Small vertex cost in dense grass (one texture fetch per vertex and a short curve), the near grass strips add vertices; the grass state is one small pass per frame (256x256 texels, 1.5 MB).</p>"
   }
  ],
  "media": []
 },
 "shadercache": {
  "group": "Foliage and loading",
  "title": "Shader cache",
  "cvar": "r_shaderProgramCache 1",
  "how": "Linked GLSL programs are saved with <code>GL_ARB_get_program_binary</code> and loaded on the next start instead of being compiled. Per-draw shader permutations are built only when a level actually needs them. Entries for a different driver or changed shaders are discarded automatically.",
  "sections": [
   {
    "title": "Shader program cache",
    "html": "<p>Caches linked Rend2 shader programs on disk so later starts and <code>vid_restart</code> operations can load driver binaries instead of recompiling every permutation.</p>\n<p class=\"req\"><strong>Requirements.</strong> Requires driver support for <code>GL_ARB_get_program_binary</code> and at least one supported program-binary format. Unsupported drivers simply fall back to normal compilation.</p>\n<h4>Main controls</h4>\n<ul><li><code>r_shaderProgramCache 0 | 1</code> — compile every time / use the on-disk program cache. Requires <code>vid_restart</code>.</li><li><code>r_shaderProgramCacheMaxMB &lt;MB&gt;</code> — maximum cache size; least-recently-used entries are evicted first.</li></ul>\n<p>The cache has no effect on frame performance. It can substantially reduce warm startup and <code>vid_restart</code> time. Driver or shader changes automatically invalidate incompatible entries.</p>"
   }
  ],
  "media": []
 },
 "telekinesis": {
  "group": "Force powers",
  "title": "Telekinesis",
  "cvar": "setForceTelekinesis 3",
  "how": "",
  "sections": [
   {
    "title": "Telekinesis",
    "html": "<p>Grabs an NPC or an object and lets you move it in space, like Grip in The Force Unleashed. Unlike Force Grip it does no damage to the NPC.</p>\n<ul><li>If you release the NPC or object while moving it, it is thrown in that direction.</li><li>Works together with Push and Pull.</li><li>Physical objects added with <code>add_object</code> can be moved as well.</li><li>You can't move while holding something: the movement keys move the held target.</li></ul>\n<h4>Command</h4>\n<ul><li><code>setForceTelekinesis 3</code></li></ul>"
   }
  ],
  "media": []
 },
 "soar": {
  "group": "Force powers",
  "title": "Soar",
  "cvar": "setForceSoar 3",
  "how": "",
  "sections": [
   {
    "title": "Soar",
    "html": "<p>Gives more aerial movement, but needs some practice.</p>\n<ul><li>You can stick to a wall, not only push off it.</li><li>Wall runs and wall flips can be started when you touch a wall in mid-air.</li><li>A second jump becomes available after touching a wall (wall runs and wall flips).</li><li>You can hang in the air for a few attacks before landing.</li></ul>\n<h4>Command</h4>\n<ul><li><code>setForceSoar 3</code></li></ul>"
   }
  ],
  "media": []
 },
 "deflect": {
  "group": "Force powers",
  "title": "Deflect",
  "cvar": "setforcedeflect 3",
  "how": "",
  "sections": [
   {
    "title": "Deflect",
    "html": "<p>Protects you from blaster fire without a saber, in the way Kylo Ren stops a bolt in Episode IX.</p>\n<ul><li>Level 1: freezes a blaster bolt; it disappears afterwards.</li><li>Level 2: you can also move while holding it.</li><li>Level 3: sends the bolt back at the enemy.</li></ul>\n<h4>Command</h4>\n<ul><li><code>setforcedeflect 3</code></li></ul>"
   }
  ],
  "media": []
 },
 "chainlightning": {
  "group": "Force powers",
  "title": "Chain Lightning",
  "cvar": "setForceChainLightning 3",
  "how": "",
  "sections": [
   {
    "title": "Chain Lightning",
    "html": "<p>Lightning that holds one NPC for two seconds and passes to the next one after one second. It is also triggered by Force Grip 4 + Lightning, but can be used as a separate power.</p>\n<ul><li>Level 1: up to 2 NPCs.</li><li>Level 2: up to 5 NPCs.</li><li>Level 3: no limit.</li></ul>\n<h4>Commands</h4>\n<ul><li><code>setForceChainLightning 3</code></li><li><code>bind J +force_chain_lightning</code></li></ul>"
   }
  ],
  "media": []
 }
};
