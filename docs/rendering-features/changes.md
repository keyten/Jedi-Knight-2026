# Changes since the original audit

[All rendering settings](../rendering-features.md)

Current review: committed source through `2c4004f3f`, 2026-11-26. The last full player-reference rewrite was 2026-11-02 (`8683fe247`, `f63356ef3`), followed by the compare/help/default pass (`64a6e48d8`). Later edits were feature-specific; the most recent before this review added the FX upgrades on 2026-11-26 (`ee768b736`).

## November feature updates (2026-11-03–26)

- **Materials:** semantic surface classification, generated close-range material detail, and texture-guided stone joints/relief with optional experimental displacement. All remain opt-in. [Controls](materials.md#material-classification).
- **Characters:** cinematic Phong smoothing with a separate gameplay override; distant offscreen body/shadow culling enabled by default, with nearby, cinematic and special-view exceptions. The latter is a distant-shadow approximation. [Controls](characters.md).
- **Ray traced shadows:** software triangle/BVH tracing for sun and selected dynamic lights, soft accumulation, hybrid sun edges and an optional compacted GL 4.3 compute path. Shadow maps retain unsupported coverage. Off until the sun/light mode is selected. [Controls](shadows.md#ray-traced-shadows).
- **Water:** more continuous tall-spillway foam and baked lighting across the sheet/pool join, improved shoreline/contact motion, optional local waterfall reflections and ordinary-water body optics. Water tessellation remains opt-in; its target edge length is now 48 pixels and finest-detail distance 192 units. [Controls](water.md).
- **Smoke:** soft opaque contact, rounded billboard thickness, smooth near-camera fading, conservative FX bounds and depth ordering for compatible smoke/glass packets are enabled. Sprite batching and demand-only scene-depth copies reduce submission work. All ten feature controls are latched; a complete legacy preset remains available. [Controls and presets](effects.md#soft-smoke-contact).
- **Stock FX:** optional light from sparks/fire, heat haze, extra GPU sparks/embers and layered impact/explosion detail. These remain off; flesh and glass impacts are excluded from automatic layering. [Controls](effects.md#light-from-sparks-and-fire).
- **Lighting research:** reusable fields, persistent preparation, adaptive probes and in-game diagnostics; gated physical-sun and authored model-irradiance adapters. Stock source discovery still supplies no validated original-stock physical field. [Status and controls](lighting-tools.md).

## Enabled rendering paths (2026-11-25–26)

Fresh settings now select `r_worldSurfaceList 1`, `r_surfaceSpritesCompute 2`, `r_modelOcclusion 1`, `r_md3Instancing 1` and `r_lightallSpecialize 1`. They retain their reference/off paths and hardware/material fallbacks. Per-plant LOD intentionally changes distant geometry. Exact no-local-light specialization retains all baked/sun/material terms and switches live. Existing archived settings keep priority.

These choices do not establish a net frame-time benefit in every scene. The [batching defaults report](../rend2-enabled-batching-defaults.md) and [merged validation](../rend2-performance-defaults-and-lightall-integration.md) keep correctness evidence separate from timing claims. See [Loading](loading.md#rendering-fast-paths) for current switches and the [registration update](../rendering-control-updates-2026-11.md) for added controls and default changes.

## Original follow-up range (2026-10-28–30)

Reviewed range: `0387b3281..9787e7945`. The following list records that historical snapshot.

- **Vegetation:** standing contact is steadier; Force push/pull bends plants and leaves; attacking sabers cut supported plants into attached and falling pieces. [Controls](foliage.md).
- **Characters:** soft ragdoll limits in SP/MP and optional soft animation limits in SP. [Controls](characters.md).
- **Sun shadows:** more robust cache/filter edges, separate sharp model layers over softer scenery, and an improved experimental scrolling-cache mode. The model split defaulted on then; since 2026-11-02 it is off by default and called `r_sunShadowModelLayer`, with the older character-layer option as its mode 2. [Controls](shadows.md#sun-shadows).
- **Point/spot shadows:** keep static world depth while updating moving casters. The static-world cache defaulted on then; since 2026-11-02 it is off by default like the other speed-ups. Forward+ also skips lights outside their range earlier. [Controls](shadows.md#dynamic-light-shadows).
- **Reflections:** better emitter occlusion and foreground-safe rough blur; optional rough-surface ray sampling; fog along reflected rays, including water. Hit reuse now defaults off, and early pixel rejection defaults to automatic. [Controls](reflections.md).
- **Offline lighting:** sun/sky and hidden-light proposals, several emitter shapes, intensity/position fitting, shadow evidence, bounced light, uncertainty checks, scene-wide sampling/scheduling, and per-source contribution records. Normal gameplay still does not consume these fitted assets. [Workflow](lighting-tools.md).

## Performance defaults (2026-11-23)

The [runtime optimization audit](../rend2-optimization-defaults.md) re-enables the GL 4.3 policy, fast depth-only prepass, point-shadow traversal/layered submission, guarded skyportal culling and exact sun-cache mode 1. Both point-shadow caches remain opt-in after unresolved depth-verifier differences. Saved explicit settings remain unchanged. Forward+, water tessellation and experimental scrolling/disk sun caches remain opt-in. The historical default-off list below describes November 2, not the current performance defaults.

## Compare cvars, help and defaults (2026-11-02)

- **One compare cvar per feature**, `r_<feature>Compare`: 0 normal, 1 split screen (original left of `r_compareSplit`), 2 off without `vid_restart`, 3+ feature views. Not saved. `r_compare` lists them; `r_help <cvar>` explains any renderer cvar. [How it works](../rendering-features.md#comparing-with-the-original).
- **Invalid values are refused** with the cvar's help (description, usage, default, example). SP now has cvar descriptions too.
- **Every new feature is off by default**, including the speed-ups. Turned off: `r_colorGrading`, `r_brdfEnergyCompensation` (it now also covers the diffuse / specular energy split, which was always on), `r_specularAA`, `r_sunShadowMode`, `r_shadowCache`, `r_shadowStaticCache`, `r_shadowSinglePass`, `r_shadowLayered`, `r_skyPortalCull`, `r_weatherCull`, `r_rainLOD`, `r_spotLights`, `r_spotLightCookies`, `r_volumetricEmission`, `r_foliageForceWaves`, `r_foliageSaberCut`, `r_weatherPuddleRipples`, `r_waterCausticsMode`, `r_gl43`, `r_shaderProgramCache`, `r_shaderProgramLazy`, `r_shaderCompileProcesses`, `r_loadPrefetch`, `r_loadThreads`, `r_materialDDCObjects`, `r_uploadPBO`, `fs_looseImageIndex`, `ui_lazyMenuAssets`. Hair and eye shading, which had no switch, is now `r_materialModels 0`. `r_refractionChromaticAberration -1` restores Rend2's own fixed fringes. The MP `r_forceToneMapAvg` / `r_forceAutoExposureMin` / `Max` debug values are back to Rend2's.
- **Renamed:** `r_smaa_quality` → `r_smaaQuality`, `r_sunShadowSplit` → `r_sunShadowModelLayer` (the old `r_shadowPCSSCharacterLayers` is its mode 2), `r_entitySunSplit` → `r_entitySunLight` (neither is a split screen).
- **Removed:** `r_shadowPCSSCharacterLayers` (see above), `r_waterSurfaceSplit` (→ `r_compareSplit`), `r_waterWaveChoppiness` (it had no effect).
- **Debug cvars only show debug views.** Moved to compare cvars: `r_toneMapDebug 1`, `2` (→ `r_toneMapCompare 1`, `3`; the remaining views are now 1–3), `r_pbrEnergyDebug 5`, `r_weatherSurfaceDebug 4`, `r_waterSurfaceDebug 9`, `r_waterWaveDebug 8`, `r_pomSilhouetteDebug 9`, `r_rainLensDebug 4`. `r_aoCompare`, `r_ssrCompare`, `r_ssgiCompare`, `r_skinSSSCompare` and `r_colorGradingCompare` keep their names with the new 0 / 1 / 2 meaning; `r_aoCompare 1` now shows what `r_ssao` gives on the left. They used to be saved: a value left in your config is reset to 0 at startup and no longer saved.

## Cvars of the audited range

The range adds **22 cvars and 3 console commands**. Their first registration lines were checked with `git blame`; the table below extends the [original provenance snapshot](../rendering-cvar-audit.md). Defaults and limitations for current use are in the topic pages.

| Name | Introduced | Registration |
| --- | --- | --- |
| `r_foliageSaberCutReset` | [`fe03423c3`](https://github.com/keyten/Jedi-Knight-2026/commit/fe03423c34d97e2717f21d9171971dcd07d82140) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/fe03423c34d97e2717f21d9171971dcd07d82140/code/rd-rend2/tr_init.cpp#L2052) |
| `r_foliageSaberCutTest` | [`fe03423c3`](https://github.com/keyten/Jedi-Knight-2026/commit/fe03423c34d97e2717f21d9171971dcd07d82140) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/fe03423c34d97e2717f21d9171971dcd07d82140/code/rd-rend2/tr_init.cpp#L2053) |
| `r_lightReconPreview` | [`27119e46e`](https://github.com/keyten/Jedi-Knight-2026/commit/27119e46e1a155a5629d243a4f5c2f1c15895719) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/27119e46e1a155a5629d243a4f5c2f1c15895719/code/rd-rend2/tr_init.cpp#L2093) |
| `cl_rendererFoliageCut` | [`fe03423c3`](https://github.com/keyten/Jedi-Knight-2026/commit/fe03423c34d97e2717f21d9171971dcd07d82140) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/fe03423c34d97e2717f21d9171971dcd07d82140/code/cgame/cg_main.cpp#L402) |
| `cl_rendererFoliageWaves` | [`89c2d6c54`](https://github.com/keyten/Jedi-Knight-2026/commit/89c2d6c5480efa04b67f2a216b28b3846a4de899) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/89c2d6c5480efa04b67f2a216b28b3846a4de899/code/cgame/cg_main.cpp#L400) |
| `r_animSoftLimitBounce` | [`7fc7d0d1d`](https://github.com/keyten/Jedi-Knight-2026/commit/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d/code/rd-rend2/tr_init.cpp#L3654) |
| `r_animSoftLimitMargin` | [`7fc7d0d1d`](https://github.com/keyten/Jedi-Knight-2026/commit/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d/code/rd-rend2/tr_init.cpp#L3653) |
| `r_animSoftLimits` | [`7fc7d0d1d`](https://github.com/keyten/Jedi-Knight-2026/commit/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d/code/rd-rend2/tr_init.cpp#L3650) |
| `r_animSoftLimitZone` | [`7fc7d0d1d`](https://github.com/keyten/Jedi-Knight-2026/commit/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d/code/rd-rend2/tr_init.cpp#L3652) |
| `r_foliageForceWaves` | [`89c2d6c54`](https://github.com/keyten/Jedi-Knight-2026/commit/89c2d6c5480efa04b67f2a216b28b3846a4de899) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/89c2d6c5480efa04b67f2a216b28b3846a4de899/code/rd-rend2/tr_init.cpp#L2707) |
| `r_foliageForceWaveStrength` | [`89c2d6c54`](https://github.com/keyten/Jedi-Knight-2026/commit/89c2d6c5480efa04b67f2a216b28b3846a4de899) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/89c2d6c5480efa04b67f2a216b28b3846a4de899/code/rd-rend2/tr_init.cpp#L2710) |
| `r_foliageSaberCut` | [`fe03423c3`](https://github.com/keyten/Jedi-Knight-2026/commit/fe03423c34d97e2717f21d9171971dcd07d82140) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/fe03423c34d97e2717f21d9171971dcd07d82140/code/rd-rend2/tr_init.cpp#L2725) |
| `r_foliageSaberCutPieces` | [`fe03423c3`](https://github.com/keyten/Jedi-Knight-2026/commit/fe03423c34d97e2717f21d9171971dcd07d82140) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/fe03423c34d97e2717f21d9171971dcd07d82140/code/rd-rend2/tr_init.cpp#L2728) |
| `r_foliageSaberCutPieceTime` | [`fe03423c3`](https://github.com/keyten/Jedi-Knight-2026/commit/fe03423c34d97e2717f21d9171971dcd07d82140) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/fe03423c34d97e2717f21d9171971dcd07d82140/code/rd-rend2/tr_init.cpp#L2731) |
| `r_ragNeutralPull` | [`7fc7d0d1d`](https://github.com/keyten/Jedi-Knight-2026/commit/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d/code/rd-rend2/tr_init.cpp#L3649) |
| `r_ragSoftLimits` | [`7fc7d0d1d`](https://github.com/keyten/Jedi-Knight-2026/commit/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d/code/rd-rend2/tr_init.cpp#L3646) |
| `r_ragSoftLimitStiffness` | [`7fc7d0d1d`](https://github.com/keyten/Jedi-Knight-2026/commit/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d/code/rd-rend2/tr_init.cpp#L3648) |
| `r_ragSoftLimitZone` | [`7fc7d0d1d`](https://github.com/keyten/Jedi-Knight-2026/commit/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/7fc7d0d1d2cc8d019e2c2cd4f23579b1d2e1072d/code/rd-rend2/tr_init.cpp#L3647) |
| `r_shadowPCSSCharacterLayers` | [`c08d838c1`](https://github.com/keyten/Jedi-Knight-2026/commit/c08d838c12b98ec78a2b8d57c35b43a0a0baa401) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/c08d838c12b98ec78a2b8d57c35b43a0a0baa401/code/rd-rend2/tr_init.cpp#L3410) |
| `r_shadowStaticCache` | [`0bb4e483f`](https://github.com/keyten/Jedi-Knight-2026/commit/0bb4e483fa4b15cbaa7e6357e8810131d0c8386b) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/0bb4e483fa4b15cbaa7e6357e8810131d0c8386b/code/rd-rend2/tr_init.cpp#L3452) |
| `r_ssrFootprintDepth` | [`5646d9528`](https://github.com/keyten/Jedi-Knight-2026/commit/5646d95287dc5fbc9bffaacabd7c1912bc93db5f) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/5646d95287dc5fbc9bffaacabd7c1912bc93db5f/code/rd-rend2/tr_init.cpp#L2579) |
| `r_ssrFusedResolve` | [`5646d9528`](https://github.com/keyten/Jedi-Knight-2026/commit/5646d95287dc5fbc9bffaacabd7c1912bc93db5f) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/5646d95287dc5fbc9bffaacabd7c1912bc93db5f/code/rd-rend2/tr_init.cpp#L2577) |
| `r_ssrReflectedFog` | [`508367f1f`](https://github.com/keyten/Jedi-Knight-2026/commit/508367f1f178d7733f279b55a8847cc42a446b92) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/508367f1f178d7733f279b55a8847cc42a446b92/code/rd-rend2/tr_init.cpp#L2594) |
| `r_ssrStochastic` | [`f22329b4b`](https://github.com/keyten/Jedi-Knight-2026/commit/f22329b4b7b747fc3daafe12b47fad92fc5c634a) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/f22329b4b7b747fc3daafe12b47fad92fc5c634a/code/rd-rend2/tr_init.cpp#L2591) |
| `r_sunShadowSplit` | [`02aa80a7f`](https://github.com/keyten/Jedi-Knight-2026/commit/02aa80a7f4ae32cb36a0f584e7f3acb7fb6da673) | [source](https://github.com/keyten/Jedi-Knight-2026/blob/02aa80a7f4ae32cb36a0f584e7f3acb7fb6da673/code/rd-rend2/tr_init.cpp#L3456) |
