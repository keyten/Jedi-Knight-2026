# Color, exposure and post-processing

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Tone mapping

*Requires `r_hdr 1` and `r_toneMap 1`, because this maps the extra brightness of HDR rendering onto your display.*

Keeps bright skies, lamps and saber glows readable instead of clipping them to flat white. Compared with the original game, ACES gives stronger contrast; AgX-like keeps colors more neutral. Changing the curve has no meaningful frame-time cost.

**Main controls**

- `r_autoExposure 0 | 1` — manual / automatically adapted exposure.
- `r_exposureCompensation <stops>` — overall brightness bias; +1 doubles exposure.
- `r_cameraExposure <value>` — legacy manual exposure, used in camera mode 0.
- `r_toneMapMode 0 | 1 | 2` — 0 legacy Rend2 filmic, 1 ACES fitted, 2 AgX-like.

**Debug**

- `r_toneMapDebug 1` — legacy vs selected tone mapper split.
- `r_toneMapDebug 2` — legacy / ACES / AgX comparison.
- `r_toneMapDebug 3..5` — raw HDR, exposure-adjusted and exposure false-color views.

A tone mapper is a brightness-and-color curve applied near the end of the frame. It compresses bright values while keeping midtones and shadow detail. [Details](../rend2-tonemapping.md).

## [For mod authors] Color grading (LUTs)

*Requires a `.cube` color table, because it supplies the new palette; without one, enabling support leaves the image unchanged.*

Changes the overall palette: make a map warmer, colder, faded or more contrasty without editing its textures. A LUT is a color lookup table: a small file that says which output color to use for each input color. Light performance cost.

**Main controls**

- `r_colorGrading 0 | 1` — disable / enable color grading.
- `r_colorGradingLUT "<file>.cube"` — select a LUT manually; empty uses `maps/<map>.cube` if present; `*identity` is neutral.
- `r_colorGradingIntensity 0..1` — blend between the original image and the LUT.

**Debug**

- `r_colorGradingCompare 0 | 1` — disable / enable split-screen comparison, original on the left.

The renderer blends the finished image with the colors from a 3D lookup table. It loads `maps/<map>.cube` automatically, or uses a chosen `.cube` file; `*identity` leaves colors unchanged. [Details](../rend2-tonemapping.md).

## Linear lighting

*Requires `r_hdr 1` and `r_toneMap 1`, because the corrected light values need the HDR output path.*

Makes light and dark surfaces combine more naturally, especially in gradients, bright rooms and shaded corners. Stock textures can look different from the original game even though the assets are unchanged. No meaningful frame-time cost.

**Main controls**

- `r_linearLighting 0 | 1` — legacy / linear-light handling of non-HDR lightmaps. Requires `vid_restart`.

**Debug**

- No separate debug controls.

Lighting is added in linear brightness rather than directly in display-encoded color. This avoids treating a display color value as a physical amount of light.

## HDR bloom

*Requires `r_hdr 1` and tone mapping (`r_toneMap` or `r_forceToneMap`), because this glow is extracted from brightness above the ordinary display range.*

Adds soft glow around bright sabers, lights and explosions. Unlike the original glow effect, it can react to actual scene brightness and glowing material surfaces. Light performance cost.

**Main controls**

- `r_bloom -1 | 0 | 1` — legacy glow / off / HDR bloom.
- `r_bloomIntensity <value>` — bloom strength.
- `r_bloomThreshold <value>` — scene-linear brightness where optional scene bloom begins.
- `r_bloomKnee <value>` — softness around the threshold.
- `r_bloomScatter <value>` — how widely the bloom pyramid spreads.
- `r_bloomSceneIntensity <value>` — contribution from bright non-emissive scene pixels; `0` keeps bloom emissive-only.

**Debug**

- No separate debug controls.

The renderer extracts bright pixels before tone mapping, blurs smaller copies of the image and blends them back. Scene bloom and glow-only bloom can be tuned separately. [Details](../rend2-bloom.md).

## Motion blur

*Requires `r_hdr 1`, because the blur runs in the HDR image before final display conversion.*

Fast turns and moving characters leave short motion streaks while menus and the HUD stay sharp. The original game has no comparable per-object velocity blur. Medium performance cost; higher resolution and sample counts increase it.

**Main controls**

- `r_motionBlur 0 | 1` — disable / enable velocity motion blur. Requires `vid_restart`.
- `r_motionBlurShutterAngle <degrees>` — exposure length.
- `r_motionBlurReferenceFps <fps>` — FPS the shutter angle refers to; `0` uses the current frame interval.
- `r_motionBlurMaxPixels <pixels>` — maximum streak length at 1080p.
- `r_motionBlurQuality 0 | 1 | 2` — low / medium / high.
- `r_motionBlurSamples <count>` — sample override; `0` uses the quality preset.
- `r_motionBlurViewModelScale <value>` — blur strength for first-person hands, weapons and saber.
- `r_motionBlurCutDistance <units>` — one-frame translation treated as a camera cut.
- `r_motionBlurCutAngle <degrees>` — one-frame rotation treated as a camera cut.
- `r_motionBlurShutterScale <value>` — gameplay exposure multiplier, used by effects such as Force Speed.

**Debug**

- `r_motionBlurReset 1` — gameplay hook that clears motion history after a cut.
- `r_motionBlurDebug 1..5` — velocity, camera/object velocity, sample count and blur contribution.

Each pixel stores how far its surface moved between frames. The renderer samples along that movement; shutter length sets how long a streak should be. [Details](../rend2-motion-blur.md).

## Camera exposure

*Requires `r_hdr 1` and tone mapping, because camera exposure controls the brightness of the scene before display conversion.*

Makes brightness behave more like a camera: use automatic exposure for play, or ISO, aperture and shutter time for a fixed shot. It does not add depth of field. Exposure math itself has no meaningful frame-time cost.

**Main controls**

- `r_cameraMode` — Camera exposure: 0 = legacy, 1 = physical manual, 2 = physical auto. Default `0`.
- `r_cameraAperture` — Physical camera aperture f-number N (manual mode; reserved for future physical DOF). Default `16`.
- `r_cameraAutoEVMax` — Maximum EV100 allowed in physical auto mode. Default `20`.
- `r_cameraAutoEVMin` — Minimum EV100 allowed in physical auto mode. Default `-6`.
- `r_cameraISO` — Physical camera ISO speed (manual mode). Default `100`.
- `r_cameraShutterSeconds` — Physical camera shutter duration in seconds (manual mode; optional motion-blur coupling). Default `0.008`.
- `r_motionBlurPhysicalShutter` — Use r_cameraShutterSeconds as motion-blur exposure duration in physical camera modes. Default `0`.

**Debug**

- `r_cameraDebug` — Print calibrated camera/meter state once per second. Default `0`.

Manual mode converts camera settings into an exposure value; automatic mode meters the scene and adapts within the chosen limits. Exposure compensation remains the simple brighten/darken control. [Details](../rend2-physical-camera.md).

## Edge smoothing, legacy glow and refraction

Smooths jagged edges, optionally adds bright-scene glow to the older glow path, and controls color separation in distortion effects. These are independent switches. Light performance cost for edge smoothing and glow.

**Main controls**

- `r_dynamicGlowBloom` — Bright-scene contribution to legacy dynamic glow; active with r_bloom -1 and r_dynamicGlow enabled. Default `0.0`.
- `r_refractionChromaticAberration` — Chromatic offset in the generic refractive-material/distortion shader. Default `0.05`.
- `r_smaa` — Disable/enable SMAA. Default `0`; `vid_restart`.
- `r_smaa_quality` — LOW | 1: MEDIUM | 2: HIGH | 3: ULTRA. Default `2`; `vid_restart`.

**Debug**

- No separate debug controls.

SMAA means subpixel morphological antialiasing: it finds image edges and blends them. Legacy glow adds a bright-image layer; refraction shifts color samples to separate red, green and blue slightly.
