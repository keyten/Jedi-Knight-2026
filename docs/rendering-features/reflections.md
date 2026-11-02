# Reflections

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Scene reflections (SSR)

*Requires `r_specularMapping 1`, because glossy materials need a reflection response; HDR is recommended to preserve bright reflections.*

Reflects nearby moving objects and lights in floors, armor and other shiny surfaces. Unlike the original static environment captures, these reflections follow what is happening in the current view. Medium performance cost; high quality and stochastic reflections can be heavy.

**Main controls**

- `r_depthPrepass 1` — needed for reflection hit reuse and the early depth-based rejection path.

- `r_ssr 0 | 1` — disable / enable hybrid SSR + cubemap reflections. Requires `vid_restart`.
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
- `r_ssrTemporal 0 | 1` — averaging across frames. Requires `vid_restart`.
- `r_ssrTemporalWeight <value>` — history weight.
- `r_ssrBlendStrength <value>` — confidence/replacement scale.
- `r_ssrHitCache 0 | 1` — reuse still-valid previous-frame ray hits; pixels are periodically retraced (default 0: measured slower than tracing every ray).
- `r_ssrReceiverCull -1 | 0 | 1` — Choose early reflection-pixel rejection: -1 automatic (default), 0 off, 1 forced.
- `r_ssrFusedResolve 0 | 1` — Combine reflection resolve with the final blend when frame averaging is off; default 1.
- `r_ssrFootprintDepth 0 | 1` — Keep rough-reflection blur from sampling closer foreground objects; default 1, needs hierarchical depth.
- `r_ssrReflectedFog 0 | 1` — Apply spatial fog between the reflector and its reflected hit; default 1. Also applies to modern water. Needs `r_volumetricFog 2` and `vid_restart`.
- `r_ssrStochastic 0 | 1` — Use changing rough-reflection rays, averaged across frames; default 0. Needs `r_ssrTemporal 1` and `vid_restart`.
- `r_ssrEmitters 0 | 1` — analytic saber/blaster/effect reflections.
- `r_ssrEmitterIntensity <value>` — analytic emitter brightness.
- `r_ssrEmitterMaxRoughness <value>` — roughness limit for analytic emitters.

**Debug**

- `r_ssrCompare 0 | 1` — cubemap-only / hybrid split screen.
- `r_ssrDebug 1..12` — material, hit, confidence, raw/cubemap/hybrid reflection, emitter and hit-cache views.

Screen-space reflections search the rendered depth image for a reflected surface, then use its color. Misses fade to environment captures, so off-screen objects are still a limitation. Optional random rough-surface rays make stretched highlights more natural but need frame averaging. [Details](../rend2-ssr.md).

## Reflection quality and reuse

*Requires scene reflections, or modern-water reflections for the water-specific settings, because these tune already active reflection passes.*

Controls the balance between reflection quality and speed. The latest defaults leave hit reuse off, select early culling automatically, and avoid rough blur leaking over a foreground object.

**Main controls**

- `r_ssrFootprintDepth` — Keep rough-reflection blur from sampling closer foreground objects; default 1, needs hierarchical depth.
- `r_ssrFusedResolve` — Combine reflection resolve with the final blend when frame averaging is off; default 1.
- `r_ssrReflectedFog` — Apply spatial fog between the reflector and its reflected hit; default 1. Also applies to modern water. Needs `r_volumetricFog 2` and `vid_restart`.
- `r_ssrResolveCache` — resolve half-res rays to radiance once per frame, then upsample (0 = per-pixel resolve). Default `1`.
- `r_ssrStochastic` — Use changing rough-reflection rays, averaged across frames; default 0. Needs `r_ssrTemporal 1` and `vid_restart`.
- `r_waterSSROptimize` — Water reflection savings, add bits: 1 lower prepass geometry detail, 2 skip hidden water, 4 use a simpler prepass shader. Default `7`.
- `r_waterSSRWaveReprojection` — Use exact previous-rendered water geometry for temporal SSR reprojection. Default `0`; `vid_restart`.
- `r_ssrHitCache 0 | 1` — Default 0: optional old-hit validation; measure before enabling.
- `r_ssrReceiverCull -1 | 0 | 1` — Choose early reflection-pixel rejection: -1 automatic (default), 0 off, 1 forced.
- `r_ssrFusedResolve 0 | 1` — Combine reflection resolve with the final blend when frame averaging is off; default 1.
- `r_ssrFootprintDepth 0 | 1` — Keep rough-reflection blur from sampling closer foreground objects; default 1, needs hierarchical depth.

**Debug**

- `r_waterMotionDebug` — Views 1..10 inspect current/previous water motion, history validity and camera-only comparison. Default `0`.

The renderer can reuse a resolved reflection image, skip pixels that need no reflection and reduce water prepass work. Hit reuse is not always faster: validating an old hit can cost as much as tracing a fresh one. [Details](../rend2-ssr-validation.md).

## Rough-surface reflections

*Requires `r_ssr 1`, `r_ssrTemporal 1` and restart, because one changing reflection ray needs several frames to settle.*

Makes reflected lights stretch naturally across a rough floor at a shallow viewing angle instead of becoming only a round blurred spot. It can look grainy before the frame history settles. Heavy compared with the ordinary reflection mode.

**Main controls**

- `r_ssrStochastic 0 | 1` — Use changing rough-reflection rays, averaged across frames; default 0. Needs `r_ssrTemporal 1` and `vid_restart`.

**Debug**

- `r_ssrCompare 1` — Compare against environment-capture reflections.
- `r_ssrDebug 7` — Inspect the raw reflection result.

The shader samples different plausible reflection directions over time and shares neighboring rays. This follows the rough surface’s highlight shape instead of tracing one mirror ray and blurring its hit. [Details](../rend2-ssr.md).

## Fog in reflections

*Requires `r_volumetricFog 2` and active scene or modern-water reflections, because the reflected ray must travel through the spatial fog.*

Makes a distant object look foggy in a reflection too, rather than clear inside a hazy room. Water reflections use the same setting even when ordinary-surface `r_ssr` is off. Light to medium additional performance cost.

**Main controls**

- `r_ssrReflectedFog 0 | 1` — Apply spatial fog between the reflector and its reflected hit; default 1. Also applies to modern water. Needs `r_volumetricFog 2` and `vid_restart`.

**Debug**

- `r_ssrDebug 7` — Inspect the reflection image before its final blend.

The renderer samples fog between the reflecting surface and the reflected hit, then applies its absorption and scattered light. Environment-capture fallback and special saber/effect reflection sources do not use this path. [Details](../rend2-water-surface.md).
