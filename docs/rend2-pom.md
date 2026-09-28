# Rend2: POM self-shadowing and adaptive traversal

Extensions of the existing parallax occlusion mapping (`r_parallaxMapping 1`), not a rewrite: the relief now shadows
direct light (sun and dynamic lights, legacy and Forward+ through one helper), the view ray can adapt its step count
to the view angle, POM can fade to normal mapping with distance, materials can give an explicit height map and a self
shadow strength, and `r_pomDebug` shows every intermediate value.

Code: `shared/rd-rend2/glsl/lightall.glsl` (`PomSurface`, `RayIntersectDisplaceMap`, `GetParallaxOffset`,
`GetPomSelfShadow`, light budget, debug views), `glsl/pom_silhouette.glsl` (`PomHit.depth`),
`shared/rd-rend2/tr_pom.cpp` (dependency warning, per draw uniforms), keywords in `tr_shader.cpp`, height packing
`R_BuildNormalHeightImage` in `tr_image.cpp`, defines in `tr_glsl.cpp`.

Off by default. With `r_pomSelfShadow 0`, `r_pomAdaptiveSteps 0`, `r_pomFadeEnd 0` and `r_pomDebug 0` POM runs the
legacy 16 + 8 step traversal with the same arithmetic (CPU replica: 0 mismatches in 60 000 rays), and every lightall
permutation without `USE_PARALLAXMAP` is token-identical after preprocessing (20 permutations checked).

## The legacy algorithm (unchanged baseline)

- Height storage: a `normalHeightMap` (or an auto-found `_nh` image) has the height in alpha, white = high.
  `R_FindImageFile` flips it (`255 - a`) and the upload swizzles R and A (`RawImage_SwizzleRA`): on the GPU **`.r` is
  the depth s in [0, 1] below the top of the relief, 0 = top**, the normal is in `.agb`. Normal+height images stay
  uncompressed with alpha (LATC is only used for plain normal maps).
- Vertex shader: the tangent space view vector `Vt` (TBN of the vertex, scaled by the normal map aspect
  `(max(1, h/w), max(1, w/h))`), packed into `var_LightDir.w`, `var_Normal.w`, `var_ViewDir.w`.
- `GetParallaxOffset`: `ds = -Vt.xy / Vt.z * parallaxDepth` (`u_NormalScale.a`, keyword `parallaxDepth`, default
  `r_baseParallax`), the texture coordinate at depth s is `fract(uv - parallaxBias * ds) + ds * s`.
- `RayIntersectDisplaceMap`: 16 linear steps (the start is shifted so a sample falls on the tile border), 8 binary
  steps, a final secant; all samples `textureGrad` with the screen derivatives of the undisplaced coordinate. The
  returned offset is `ds * (s_hit - parallaxBias)`; material maps are then sampled at the displaced coordinate.
- `velocity.glsl` has its own copy (alpha tested surfaces only), still fixed 16 + 8.

## Architecture

### PomSurface: the hit shared by every light

`RayIntersectDisplaceMap` now also stores the virtual hit in a global `PomSurface g_pom`: displaced coordinate (in the
wrapped height field space), hit depth s, the frame the relief is extruded along (T, B, N), `scale = aspect *
parallaxDepth * fade`, the texture gradients, the distance fade and sample counters. There is no second view ray.
Silhouette POM shells fill the same structure from their `PomHit` (group frame, `depth = s0 + dir.z * t`, shell
gradients), so self shadowing works on SPOM shells too — the structure is the interface any future relief technique
(tessellated displacement, better SPOM) has to fill.

### GetPomSelfShadow(L)

The one self shadow routine, called with the world direction towards a light:

1. `Lt = (L·T, L·B, L·N)`. Below 0.05 (≈3°) elevation the visibility fades to 0: a ray towards a light under the base
   plane always ends in the relief, and nearly horizontal rays would have to cross many tiles.
2. Start at `s0 = s_hit - r_pomSelfShadowBias` (slightly above the virtual surface), march `r_pomSelfShadowSteps`
   equal depth steps up to the top: the coordinate advances by `Lt.xy / Lt.z * scale` per unit of s climbed (the
   mirror of the view ray, `+L` instead of `-V`).
3. Soft visibility: `occlusion = max_i (s_i - h_i) * softness * (1 - f_i)` — the penetration of the ray below the
   height field, nearer occluders weigh more (Tatarchuk 2006 style), `visibility = 1 - saturate(occlusion)`. The loop
   stops at full occlusion. `r_pomSelfShadowSoftness` large = hard shadows.
4. `mix(1, visibility, strength)`, strength = `r_pomSelfShadowStrength` × material `pomSelfShadow` × distance fade,
   clamped to [0, 1]: the result never exceeds 1 and never goes negative.

All height samples use `textureGrad` with the stored gradients, so the routine is valid inside the dynamic light loop
(non-uniform control flow) and uses the same mip as the view ray.

### Where it applies (direct light only)

| light | how | code |
|---|---|---|
| sun | `shadowValue *= GetPomSelfShadow(sunDir)`: part of the sun visibility, next to cascades and contact shadows; so it applies wherever the sun shadow applies (`r_sunlightMode 1` lightmap modulation, `2` direct primary light); no sun shadow map = no sun self shadow | `lightall.glsl` after `shadowValue` |
| dynamic lights, legacy and Forward+ | `DynamicLightReceiverVisibility` (the receiver hook of `EvaluateDynamicLight`, the single call site of both paths) returns the weighted self shadow | `lightall.glsl` |
| SSGI | nothing to do: the hook scales the attenuation before `g_ssgiDynamicDiffuse` is accumulated, so the bounce source is already self shadowed; the sun term is inside the lit colour SSGI stores | — |
| area / LTC lights | not present in this branch (Forward+ skips `type != 0`). Rule for later: one visibility towards the representative point of the emitter (closest point or centre), no area integration | — |

Not attenuated: lightmap ambient recovery, light grid ambient, cubemap / diffuse IBL, SSR, SSGI indirect light,
emissive.

### Local light budget (`r_pomSelfShadowLightMode`)

A Forward+ cluster can hold many lights; a self shadow ray for each would cost `steps` samples per light. Modes 1 and
2 give rays only to the N strongest lights **at this pixel**:

1. if the list has more than N lights, a pre-pass over the pixel's list (same list, legacy mask or cluster) keeps the
   N + 1 largest `luma(color) × attenuation(distance)` in registers (N ≤ 4);
2. in the main loop a light's self shadow weight is `smoothstep(cut, 1.5 × cut, importance)`, cut = importance of the
   (N+1)-th light. The N-th and (N+1)-th swap smoothly: deterministic per pixel, no pops, no CPU sort, identical for
   legacy lights and clusters (whose order is not by intensity).

Mode 3 gives every light a ray (debug / ultra), mode 0 only the sun.

### Adaptive view traversal and distance fade

- `r_pomAdaptiveSteps 1`: linear steps = `mix(r_pomMinSteps, r_pomMaxSteps, 1 - |Vt.z|)` (frontal few, grazing
  many), binary steps `r_pomBinarySteps`; the legacy border-sample start and secant are kept. Off = exactly 16 + 8.
- `r_pomFadeStart` / `r_pomFadeEnd` (end > start enables it): the fade factor scales the parallax depth of the view
  ray and the self shadow strength continuously, so POM flattens into normal mapping without a pop; beyond the end
  the trace is skipped. With adaptive steps the step count also drops towards 4 with the fade.
- Loops have fixed maxima (64 linear, 16 binary, 32 shadow) with uniform counts.

### Explicit height map

`heightMap <image>` in a stage: its red channel (white = high) is resampled bilinearly (wrapping) to the normal map
size and written into the alpha of a combined image (`<normal>+h<hash>`), flipped like a `_nh` file and uploaded as
`IMGTYPE_NORMALHEIGHT`. Priority: explicit `heightMap` > alpha of `normalHeightMap` / `_nh`. Without a `normalMap`
keyword a flat normal is used. No extra sampler, so ordinary POM, silhouette POM (including its automatic mode) and
the velocity pass use it unchanged; existing materials need no change.

### Compile time

The self shadow rays are inlined at every call site (sun + light loop + budget pre-pass). To keep the ~200 POM lightall
permutations of SP from growing at every start, they are compiled only with `r_pomSelfShadow` set at renderer start
(`USE_POM_SELFSHADOW`), the debug views only with `r_pomDebug` set (`USE_POM_DEBUG`) — both cvars are latched
(`vid_restart`), like `r_forwardPlusDebug`. Traversal, fade and budget parameters are per draw uniforms.

Measured with the offline checker (lightmap + deluxe + MR + cube + SSAO + sun cascades + primary light + dshadows +
SSR + SSGI POM permutation, min of 12 interleaved runs, includes context start-up of ~1 s / ~0.4 s):

| program | Intel UHD | RTX 2060 |
|---|---|---|
| HEAD | 1521 ms | 499 ms |
| new, self shadow off (default) | 1455 ms (= HEAD within noise) | 484 ms |
| new, `USE_POM_SELFSHADOW` | 1658 ms (+~140 ms) | 519 ms (+~25 ms) |
| new, self shadow + `USE_POM_DEBUG` | 1712 ms | 500 ms |

With `r_pomSelfShadow 1` the SP start therefore grows by roughly 200 POM programs × 0.14 s ≈ 30 s on the Intel iGPU
(a few seconds on NVIDIA); with it off, nothing.

## Material syntax

```
textures/test/bricks
{
	{
		map textures/test/bricks.tga
		normalMap textures/test/bricks_n.tga
		heightMap textures/test/bricks_h.tga   // optional: explicit height, priority over the _nh alpha
		parallaxDepth 0.06
		pomSelfShadow 0.8                      // 0..1, default 1; 0 = no self shadow for this material
	}
}
```

## Cvars

| cvar | default | |
|---|---|---|
| `r_pomSelfShadow` | 0 | self shadowing of direct light, **latched**; needs `r_parallaxMapping 1` (warns once, never enables it; starts working when parallax is turned on) |
| `r_pomSelfShadowLightMode` | 1 | 0 sun only, 1 sun + strongest local light, 2 sun + `r_pomSelfShadowMaxLocalLights`, 3 all lights |
| `r_pomSelfShadowMaxLocalLights` | 2 | N for mode 2 (1–4) |
| `r_pomSelfShadowSteps` | 12 | samples per shadow ray (4–32) |
| `r_pomSelfShadowStrength` | 1 | 0 none … 1 physical, × material `pomSelfShadow` |
| `r_pomSelfShadowBias` | 0.02 | ray start above the hit, in relief depth units |
| `r_pomSelfShadowSoftness` | 4 | penetration → shadow scale; higher = harder |
| `r_pomAdaptiveSteps` | 0 | view ray steps by angle; 0 = legacy 16 + 8 |
| `r_pomMinSteps` / `r_pomMaxSteps` / `r_pomBinarySteps` | 8 / 32 / 8 | adaptive traversal limits |
| `r_pomFadeStart` / `r_pomFadeEnd` | 0 / 0 | distance fade to normal mapping (off while end ≤ start) |
| `r_pomDebug` | 0 | cheat, **latched**: 1 raw height, 2 displaced UV, 3 view ray hit depth (white = top), 4 sun self shadow, 5 darkest local light self shadow, 6 view ray samples (heat), 7 self shadow samples (heat), 8 distance fade |
| `r_pomDebugFreezeLight` | 0 | cheat: keep the sun direction of the moment it was set |

Dependencies: `r_parallaxMapping 1` (runtime), `r_normalMapping 1` (POM permutations only exist with it), a
`normalHeightMap` / `_nh` / `heightMap`; sun self shadow needs `r_sunlightMode 1|2`; Forward+, SSGI, SSR, LTC are
optional (see the table above).

## Known artifacts

- Only the height field occludes: world geometry is the job of the shadow maps; the relief never shadows neighbouring
  surfaces.
- Thin occluders between two samples are missed at low elevation (12 steps: up to ~3 % disagreement with a brute-force
  ray on a fine sine field at 10°); raise `r_pomSelfShadowSteps` for such materials.
- Height maps are tiled: the shadow ray wraps across the tile like the view ray.
- Soft shadows are a penetration heuristic, not an integration over the light size.
- Budget modes blend a light's self shadow in over a 1–1.5× importance band; two nearly equal lights can both be
  partly self shadowed.
- Area lights would use a single representative direction.
- `r_pomSelfShadow` / `r_pomDebug` need `vid_restart` to be compiled in; turning `r_pomSelfShadow` off after a start
  with it on still disables it at runtime (strength 0).
- `velocity.glsl` (alpha tested POM surfaces) keeps the fixed 16 + 8 traversal: with adaptive steps the alpha test
  of the velocity pass can differ by a step from the colour pass.

## Performance

Not measured in game (session limit: builds and offline checks only). Cost model per POM pixel: view ray 16 + 8 + 2
samples (legacy) / 10–42 (adaptive); a self shadow ray ≤ `r_pomSelfShadowSteps` samples, typically fewer (early out at
full occlusion, none on the relief top: the harness averaged 0.3–1.6 samples on bricks / panels, 4–12 on a dense
bump field); the budget pre-pass costs one light fetch per light of the list when the list has more than N lights.

To fill in (ms, GPU, `r_speeds` / frame time, a POM wall frontal and at a grazing angle):

| configuration | frontal | grazing |
|---|---|---|
| POM legacy | | |
| POM adaptive (8–32) | | |
| POM + sun self shadow | | |
| POM + sun + 1 local | | |
| POM + sun + 4 local (mode 2, N 4) | | |
| POM + all lights (mode 3) | | |

## Validation done

- MSVC Release SP + MP and gcc (w64devkit ninja) SP + MP build without new warnings; A/B DLLs `build/ab/*-pom.dll`.
- Offline GLSL (Intel UHD + RTX 2060): lightall POM permutations (lightmap / vector / vertex × MR / SG+dshadows /
  cloth × no sun / sun modulate / primary light × plain / SSR+SSGI / SSAO+F+ debug × self shadow / debug defines) and
  the silhouette POM variants with and without self shadow compile and link: 668 compiles, 0 failures.
- Preprocessed lightall without `USE_PARALLAXMAP` token-identical to HEAD (20 permutations).
- CPU harness (float replica): refactored traversal with 16 / 8 = legacy offsets bit for bit; hard-limit self shadow
  vs a 4096 step brute-force ray on bricks / sine bumps / slotted panel at 10–70° elevation and parallax depth 0.05 /
  0.1: 97.2–100 % agreement; softness monotonic.

## In-game checklist

1. `r_parallaxMapping 0`, `r_pomSelfShadow 1`, `vid_restart`: one warning, nothing changes; `r_parallaxMapping 1`:
   self shadows appear without another restart.
2. `r_pomSelfShadow 0` (and `r_pomAdaptiveSteps 0`): POM looks exactly like before (A/B DLLs).
3. Brick wall / deep grooves / metal panel / floor with the sun (`r_sunlightMode 2`, then 1): shadows fall on the side
   away from the sun and move with it; `r_pomDebug 4`, `r_pomDebugFreezeLight 1` to compare.
4. Point light / saber moved along a POM wall: grooves shadow away from the light, not like static AO; `r_pomDebug 5`.
5. `r_forwardPlus 0/1`: identical self shadows; many lights: `r_pomSelfShadowLightMode 1/2/3`, no popping when lights
   swap rank.
6. `r_ssgi 1`: the bounce from a dynamic light near a POM wall carries the self shadow; no double darkening.
7. Frontal and grazing views, `r_pomAdaptiveSteps 1`, `r_pomDebug 6`: fewer samples frontal, more grazing, no layering.
8. `r_pomFadeStart 512`, `r_pomFadeEnd 1024`, `r_pomDebug 8`: POM flattens gradually, no pop.
9. Silhouette POM material with `r_pomSilhouette 1`: shells are self shadowed like the base surfaces in the crossfade.
10. A material with `pomSelfShadow 0` and one with `heightMap`: no self shadow / explicit height used.
11. Fill in the performance table.
