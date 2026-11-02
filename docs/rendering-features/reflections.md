# Reflections

[All rendering settings](../rendering-features.md)

Reflections in floors, armor and other glossy surfaces. Water reflections are on [Water](water.md).

## Screen-space reflections (SSR)

Rend2's reflections come from cubemaps: 360° snapshots taken at a few points of the map when it loads (only with `r_cubeMapping 1`, which is off by default). They never show characters, sabers or anything that moves, and they are often misplaced. SSR reflects what is actually on screen: a polished floor mirrors the duelists standing on it, and a lit corridor shows in a wet wall. What SSR can't see — things off screen or behind other objects — falls back to the cubemap, or to no reflection without cubemaps. Works best with `r_hdr 1`; without it bright reflections are clipped.

**Cost:** medium; high quality and rough-surface rays can be heavy.

- `r_ssr 0` *(restart)* — 1 on.
- `r_ssrQuality 1` — 0 low, 1 medium, 2 high, 3 ultra.
- `r_ssrTemporal 0` *(restart)* — 1: average reflections over frames. Smoother and less noisy, with slight ghosting when things move fast. Needed for [rough reflections](#rough-reflections).
- `r_ssrMaxRoughness 0.6` — surfaces rougher than this use only the cubemap; lower it if blurry reflections look wrong.
- `r_ssrEmitters 1` — sabers, blaster bolts and other glowing effects are reflected even when off screen.
- `r_ssrEmitterIntensity 1` — their brightness.

**Compare:** `r_ssrCompare` — 1 split screen (cubemap reflections left), 2 off.

**How it works.** For each glossy pixel the shader marches a ray through the on-screen depth image until it hits a surface, and uses that pixel's color as the reflection. A confidence value fades SSR out near screen edges and where the ray misses, so it blends into the cubemap without a seam. Sabers and effects are added separately as analytic light sources, which is why they reflect from off screen too.

- `r_ssrMaxDistance 1024` — longest reflection ray, in world units.
- `r_ssrThickness 8` — how thick on-screen surfaces are assumed to be; too low lets rays pass behind thin objects.
- `r_ssrEdgeFade 0.1` — fade band at screen edges.
- `r_ssrBlendStrength 1` — 0 cubemap only … 1 full SSR where reliable.
- `r_ssrRoughOcclusion 1` *(restart)* — on rough surfaces SSR keeps the cubemap's darkening in baked shadow; 0 lets light reflections "ghost" on shadowed walls.
- `r_ssrEmitterMaxRoughness 0.35` — rougher surfaces don't reflect sabers and effects (their dynamic light already makes a highlight there).
- `r_ssrSteps 0`, `r_ssrRefineSteps 0` — march and refinement steps; 0 from the quality.
- `r_ssrHalfRes -1` — trace at half resolution: -1 from the quality, 0 full, 1 half.
- `r_ssrHiZ -1` — faster hierarchical tracing: -1 from the quality, 0 off, 1 on.
- `r_ssrTemporalWeight 0.9` — how much history frame averaging keeps.
- `r_depthPrepass 1` — the depth pre-pass SSR, AO and contact shadows rely on; keep it on.

Speed options. Defaults are measured to be fastest; change only to compare.

- `r_ssrReceiverCull -1` — skip pixels that need no reflection early: -1 automatic, 0 off, 1 always.
- `r_ssrResolveCache 1` — turn half-resolution hits into colors once per frame, then upsample.
- `r_ssrFusedResolve 1` — without frame averaging, resolve reflections inside the final blend instead of a separate pass.
- `r_ssrHitCache 0` — 1: reuse still-valid hits from previous frames. Checking an old hit costs about as much as tracing a new one, so it is off.
- `r_ssrFootprintDepth 1` — blurry reflections don't smear a foreground object into the background. Needs hierarchical tracing.

**Debug**

- `r_ssrDebug 1..12` — normal, roughness, reflectance, hit/miss, hit distance, confidence, raw SSR, cubemap, combined, replaced part, saber/effect reflections, reused hits.

Design notes: [rend2-ssr.md](../rend2-ssr.md), [validation](../rend2-ssr-validation.md).

## Rough reflections

On a rough floor, a lamp's reflection should stretch into a long streak toward you at shallow angles. Ordinary SSR traces one mirror ray and blurs it, which gives a round smudge instead. This mode traces rays scattered the way a rough surface really scatters light, so reflections take the right shape. It looks grainy for a moment after fast movement, until the frame history settles. Needs `r_ssr 1` and `r_ssrTemporal 1`.

**Cost:** heavy compared with ordinary SSR.

- `r_ssrStochastic 0` *(restart)* — 1 on.

No compare cvar: the rough rays need their own passes and history; `r_ssrCompare` compares SSR as a whole.

**How it works.** Each pixel traces one ray in a random direction drawn from its surface's roughness, shares results with its neighbours, and averages over frames.

**Debug**

- `r_ssrDebug 7` — raw reflection before blending.

## Fog in reflections

With volumetric fog, a distant object seen in a reflection should be as hazy as when you look at it directly. This makes the reflected ray pass through the fog too. It is on whenever SSR or modern water is; also used by water reflections. Needs `r_volumetricFog 2`.

**Cost:** light to medium.

- `r_ssrReflectedFog 1` *(restart)* — 0 off.

**Compare:** `r_ssrReflectedFogCompare` — 1 split screen (no fog in reflections left), 2 off (also for water reflections).

**How it works.** The fog between the reflecting surface and the reflected hit is sampled along the ray and applied to the reflection. Cubemap fallback and saber/effect reflections don't get it.
