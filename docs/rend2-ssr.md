# Rend2: screen-space reflections

Hybrid reflections for opaque PBR materials: screen-space reflections (SSR) where the screen has the reflected
surface, the existing parallax corrected, roughness prefiltered cubemap reflections everywhere else. SSR
**replaces** part of the cubemap reflection, it is never added on top of it.

Code: `shared/rd-rend2/tr_ssr.cpp`, shaders `ssr_common.glsl` (library), `ssr_hiz.glsl`, `ssr_downsample.glsl`,
`ssr_trace.glsl`, `ssr_resolve.glsl`, `ssr_temporal.glsl`, `ssr_composite.glsl`, `ssr_debug.glsl`; material output
in `lightall.glsl`.

**Shared infrastructure** (`tr_screenspace.cpp`, also used by the screen-space GI, see `rend2-ssgi.md`): the normal
attachment (2), the MSAA resolve of depth and attachments, the linear depth + closest-depth Hi-Z pyramid
(`screenHiZ`), the color-0-only composite FBO, the ray march (`SSRSetupRay` / `SSRMarchRay` in `ssr_common.glsl`,
pass-specific steps / thickness / step distribution as arguments), the history cut detection and the GPU timers.
They exist when `r_ssr` **or** `r_ssgi` is on; neither requires the other. With SSGI on, every opaque lightall
fragment writes its normal, while the SSR receiver flag (`a`) keeps its meaning, so the SSR output does not change.

Off by default (`r_ssr 0`, latched): no resources are created, lightall is compiled exactly as before (the
preprocessed source without `USE_SSR` is identical to the previous one), renderFbo has its two old attachments and
the main pass is drawn in one piece. The image is unchanged.

## Before / after

**Before.** Rend2 is a forward renderer. `lightall.glsl` computes everything in one go, including the specular
image based lighting of `CalcIBLContribution`:

```
R        = reflect(-E, N) - (cubemapOrigin - viewOrigin + viewDir) / parallaxRadius   // parallax correction
radiance = textureLod(u_CubeMap, R, roughness * ROUGHNESS_MIPS)                        // prefiltered mips
radiance *= clamp(luma(lighting) / radiance.a, 0, 1)                                   // lighting scale
C        = radiance * W,   W = specularAO * EnvBRDF.x + EnvBRDF.y   (cloth: EnvBRDF.b)
out_Color += C
```

The cubemap of a surface is the closest one (`R_CubemapForPoint`, per BSP surface / per entity). Normals and
roughness only exist while the fragment is shaded; nothing is kept in screen space.

**After.** lightall still adds `C`. With `r_ssr 1` it also writes a small material buffer (three extra
attachments of renderFbo, no G-buffer). After the opaque surfaces of the view are drawn, the SSR passes find a
reflection ray hit with a confidence `c` and composite

```
color += c * (SSR radiance * W - C)
```

`c = 1`: the pixel shows the SSR under exactly the BRDF weight the cubemap reflection had (Fresnel, F0, EnvBRDF,
specular occlusion), and the cubemap part is removed. `c = 0`: the pixel stays as lightall wrote it, with the
cubemap reflection. Everything in between is a linear blend, so a miss is never black and never doubled.
Metalness is not a mask: `W` already holds F0 and the Fresnel response, so polished dielectrics reflect, rough
paint barely does, and pixels with a negligible `W` are not traced.

The replacement equation is linear, but the current MSAA inputs are not a physically exact per-sample resolve:
`C` is averaged linearly, whereas octahedral normals and `sqrt(W)` are averaged in their encoded forms, and
hardware depth comes from one sample. The stored-depth check rejects many inconsistent edge pixels; it does
not repair mixed normals or weights. A representative-sample material/depth resolve remains deferred.

## Material buffer (renderFbo attachments, only with `r_ssr 1`)

| attachment | image | format | contents |
|---|---|---|---|
| 0 | `renderImage` | RGBA16F / RGBA8 | scene color (unchanged) |
| 1 | `glowImage` | as color | glow (unchanged) |
| 2 | `ssrNormalImage` | RGB10_A2 | rg = octahedral world normal (after normal mapping), b = roughness (as lightall uses it), a = receiver |
| 3 | `ssrSpecularImage` | RGB10_A2 | rgb = sqrt(W) |
| 4 | `ssrCubemapImage` | RGBA16F | rgb = C (the cubemap reflection lightall added), a = view depth of the fragment |

- 10 bit octahedral normals: 8 bits give about 0.7 degrees of error, which shows as wobble in mirror floors (the
  reflection doubles the angle); 10 bits about 0.17 degrees.
- `W` is in [0, 1]; the square root keeps about 5% precision at dielectric values (0.02 - 0.04).
- `C` is HDR and needs a float format. Storing `C` and `W` (6 values) is the minimum for an exact replacement: the
  cubemap is chosen per surface with a per draw parallax, so it cannot be looked up again in screen space.
- The view depth in `.a` validates the data: a pixel is only a receiver when it matches the depth buffer (2% +
  1 unit). This rejects data of a surface later covered by a non-lightall surface (possible without the depth
  prepass) and the first person weapon (drawn with a hacked depth range).
- 16 extra bytes per pixel and sample.

Only opaque lightall stages write the attachments (`RenderState::ssrAux`, set by `RB_WritesSSRMaterial` in
tr_shade.cpp: lightall program, no blending or ONE/ZERO, no color mask, SSR view). All other draws have them
masked with `glColorMaski` (`GL_SetSSRAuxWrite`): a shader that does not write an output leaves undefined values.
`GL_State` re-masks them after every `qglColorMask` (which sets the masks of all draw buffers), as do the raw
`qglColorMask` calls of the depth prepass and the anaglyph code. Stages without per-pixel lighting or without a
specular map write "not a receiver" (lightall has no specular reflections there either).

With `r_cubeMapping 0`, or on surfaces without a cubemap, `C = 0` and `W` is still computed (the EnvBRDF LUT is
bound for lightall whenever SSR is on): SSR adds reflections where there were none, and a miss keeps the old
look.

## Render pass order (main world view, `r_ssr 1`)

1. `RB_BeginDrawingView`: clears color/depth/glow as before; `RB_SSRBeginView` decides whether the view gets SSR
   (only views rendered into renderFbo with a plain perspective projection: not portals/mirrors (oblique near
   plane), sky portals, cubemap or shadow views, no `RDF_NOWORLDMODEL`/hyperspace) and clears attachments 2-4.
2. Depth prepass (+ velocity), screen-space AO / contact shadows: unchanged.
3. Main pass, **opaque part**: `RB_SubmitRenderPass` sorts the draw items as before and splits at the first item
   of a later sort, or of the fog pass stage of the opaque sort (see `RB_CreateSortKey`).
4. `RB_RenderScreenSpaceOpaque` (once per view, tr_screenspace.cpp):
   1. shared: MSAA: resolve depth into `renderDepthImage` and the attachments into their textures; `screenHiZ`
      mip 0 = linear view depth (`ssr_hiz` LINEARIZE), with Hi-Z tracing (SSR or SSGI) also mips 1-6 = closest depth
   2. screen-space GI when enabled (`rend2-ssgi.md`), composited into color 0 first
   3. `RB_RenderSSR`: MSAA color is resolved into `renderImage` when needed. `ssr_color_mask` builds
      `ssrColor` mip 0 with premultiplied coverage, excluding the first person weapon and texels outside
      the view rectangle. All six downsample levels use four bilinear taps on that masked result.
   4. classify (`ssr_classify`, `r_ssrReceiverCull 1`, or `-1` while few texels need a ray): nearest depth where a pixel does **not** need a ray; the
      trace is drawn behind it and in front of the cleared depth, so it is rejected by the early depth test there.
      Both passes test `LESS`: the earlier "mark the ray pixels, trace with `GREATER`" flipped the depth
      direction after writing, which turns the coarse (hierarchical) depth rejection of many GPUs off until the
      next clear and leaves only per-pixel early tests. The classified trace variants skip the duplicate
      receiver eligibility checks (stored depth and specular weight). The trace is cleared with confidence
      **zero**; an alpha-1 black clear would make rejected pixels look like confident cached hits. Without
      the classification the trace clears to zero too and **discards** the texels without a ray (they keep
      the miss), so both variants pass exactly the ray texels.

      `r_ssrReceiverCull -1` (default since 2026-10-29) counts them with a `GL_SAMPLES_PASSED` query on the
      trace draw (three queries in a ring, read when available, never waited for) and classifies only while
      under 12% of the trace texels need a ray, off again above 18%. Measured on Intel UHD (see
      `rend2-ssr-validation.md`): the classification pass costs about as much as the eligibility test it
      replaces in the trace, so it only pays when the early depth test packs scattered ray pixels into full
      GPU groups (sparse, speckled receivers: up to a third faster below ~16%); on contiguous receivers it lost
      ~15% at every share, and at 1600x900 with 45% ray texels about 1 ms. `r_speeds 100` prints the share.
   5. trace (`ssr_trace`, full or half resolution) -> `ssrTrace[current]`, reusing valid hits of
      `ssrTrace[previous]` (hit cache)
   6. resolve: at half resolution, `r_ssrResolveCache 1` reads the current scene color once per ray into
      `ssrRadiance`, then upsamples that radiance into full-resolution `ssrResolve`. Full resolution and
      `r_ssrResolveCache 0` retain direct per-pixel color-pyramid resolve. Without the temporal pass
      (`r_ssrFusedResolve 1`, float target, not `r_ssrDebug 6`) nothing else reads `ssrResolve`: the composite
      runs the same resolve itself (`SSRResolvePixel`, `ssr_composite_fused*`) and the full resolution RGBA16F
      write + read disappear; only the per-ray radiance pass of the resolve cache remains.
   7. optional temporal accumulation (`ssr_temporal`) -> `ssrHistory[current]`
   8. composite (`ssr_composite`) into color 0 through `ssrCompositeFbo` (color 0 only: the glow and material
      attachments stay untouched and the sampled depth is not attached). Pixels with nothing to replace or add
      are discarded rather than blended with zero.
5. Main pass, **the rest**: decals, see-through, blended surfaces, fog passes, sun, flares - on top of the hybrid
   result.
6. Post processing unchanged; `r_ssrDebug 1-6` and `12` overlays at its end.

The final MSAA color/depth blit selects attachment 0 only. A generic blit into
all enabled draw buffers would overwrite the resolved material textures with
scene color before the debug overlays sample them. Glow is resolved separately.

SSR therefore samples the opaque HDR scene before tone mapping (in the same space lightall writes: scene linear
with HDR lightmaps / `r_linearLighting`, display encoded otherwise - the same space as the cubemap reflection it
replaces), without particles or glass in it.

### Buffers of the passes

| image | format | size |
|---|---|---|
| `ssrColor` | as renderImage, 7 mips (all premultiplied coverage) | full |
| `screenHiZ` (shared) | R32F, 7 mips | full |
| `ssrTrace[2]` | RGBA16 (hit uv, log hit depth, 14 bit confidence + geometry + reused bits), depth 16 (classification) | trace resolution (half: a quarter); this frame and the previous (hit cache) |
| `ssrResolve` | RGBA16F (premultiplied radiance, confidence) | full; not written by the fused composite |
| `ssrRadiance` | RGBA16F (current-frame radiance, confidence) | trace resolution, allocated only at half res |
| `ssrHistory[2]`, `ssrHistoryGeom[2]` | RGBA16F | full, only with `r_ssrTemporal 1` |

## Tracing

- View space position from depth (`P[0], P[5], P[8], P[9], P[10], P[14]`), the normal from attachment 2, `R =
  reflect(-V, N)`. The ray starts slightly above the surface and is clipped in front of the near plane (rays
  towards and behind the camera) and to the view rectangle.
- The view space ray is projected to the screen; its screen position and 1 / depth are linear in screen space,
  so the steps are spread evenly over the covered pixels (McGuire & Mara 2014) with a per pixel jitter
  (interleaved gradient noise; it changes per frame only with `r_ssrTemporal 1`, which averages it - without the
  accumulation a changing jitter would only flicker).
- A crossing: the ray segment reaches behind a depth buffer surface but not further than its assumed thickness,
  `r_ssrThickness * (1 + z / 512)`. It is refined with `r_ssrRefineSteps` binary search steps.
- Hi-Z (`r_ssrHiZ`, from `r_ssrQuality` medium): walks the closest depth mips. Cells the ray passes entirely in
  front of are skipped at once and the walk climbs a level; near surfaces it descends to single pixels. Long rays
  over open floors need a few dozen iterations instead of hundreds of pixels. Behind thin objects it has to walk
  pixel by pixel (thickness), the iteration budget is `3 * steps`.
- Ray length: `r_ssrMaxDistance`, down to 35% towards `r_ssrMaxRoughness`.
- Roughness: the cone of the GGX lobe (alpha = roughness^2, as in the cubemap prefilter, taken as a Phong lobe)
  gives the blur footprint at the hit; the resolve samples the color pyramid at `log2(footprint in pixels)`. Mirror
  surfaces are sharp, rough ones blurred, and the sharpness matches the cubemap mip they blend with. Above
  `r_ssrMaxRoughness` only the cubemap is used (smooth fade from 70% of it).
- Footprint depth (`r_ssrFootprintDepth`, default 1, Hi-Z tracing only): the pyramid blurs what the camera sees
  around the hit. A surface well in front of the hit next to it on the screen (a pillar silhouette in front of
  the reflected wall) is nowhere near the reflection lobe, which ends at the hit, yet a coarse mip blends it in:
  dark or bright halos around foreground silhouettes in rough reflections. `SSRHitRadiance` reads, at the chosen
  level, the 2x2 Hi-Z texels under the bilinear taps of the color sample (since 2026-10-29; the hit's own cell
  alone missed a pillar in the neighbouring cell that the taps still blend in). Each texel in front of the hit by
  more than `3% + 2 + 3 cell widths` (what the hit surface's own slope across the cell can explain) adds its
  bilinear weight, scaled by how far in front it is (`front = min(2 * sum, 1)`), and the mip is limited to
  `level - front`; the walk goes on to finer levels while anything is in front. The limit moves continuously
  with the hit position and the foreground depth instead of jumping a whole level. Four Hi-Z fetches when
  nothing is in front; a reflection sharper than the cone (never a halo) next to silhouettes. Surfaces behind
  the hit are left in the blur: lobe rays passing beside a reflected object do reach them. With linear tracing
  the Hi-Z mips do not exist and the test is off.
- Reflection occlusion (`r_ssrRoughOcclusion`, latched, default 1): the cubemap reflection is darkened by the
  lightmap luminance scale (`CalcIBLRadiance`) and the specular occlusion, the SSR weight was not. One mirror ray
  only sees a narrow lobe, so on rough walls in baked shadow or recesses it found a sunlit floor and replaced the
  dark reflection with a bright one: light, speckled ghosts (receiver speckles of the roughness map) cut where
  the rays leave the screen, coming and going as the view turns (mp/ffa3 door jambs and column edges, 2026-10-21).
  The SSR weight now takes the same occlusion, fading in from roughness 0.15 to 0.45; mirror surfaces keep the
  unoccluded traced reflection. 0 restores the old behaviour.

- The trace stores the hit **surface** point (its uv and log encoded view depth), not a distance: it is fixed in
  the world, so the next frame can reuse it, and the resolve derives the hit distance from it.

### Stochastic GGX (`r_ssrStochastic 1`, latched, needs `r_ssrTemporal 1`)

The mirror ray with a cone blur at the hit gives every reflection the cone's round footprint. A real GGX lobe
stretches along the view at grazing angles (a lamp over a glossy floor becomes a vertical streak) and its rays
see occlusion and parallax of their own. With `r_ssrStochastic 1`:

- Trace: each trace texel marches one direction sampled from the visible normals of the GGX lobe (Heitz 2018,
  alpha = roughness^2 as the cone), a new one every frame (interleaved gradient noise shifted by an R2 sequence over
  the frames). A sample into the surface falls back to the mirror direction. No hit cache.
- Radiance pass (the resolve cache, also at full resolution): every ray's radiance, filtered over the solid angle
  of one of 32 samples (`1 / (32 pdf)`, at most the cone), plus its view space hit point and log2 pdf
  (`*ssrRay`, RGBA16F, trace resolution).
- Resolve: every pixel reuses the rays of the nearest trace texels (half resolution 3x3, tent weights; full
  resolution a 2x2 block that moves with the frame), weighted by depth / normal similarity as before and by this
  pixel's BRDF over the pdf the ray was sampled with (`D G2 / (4 NV)` over `G1 D / (4 NV)`, ratio estimator,
  Stachowiak 2015; limited to 4 against fireflies). A miss weighs as the pixel's own ray would and adds nothing.
- Temporal: two samples of a lobe say nothing about whether the reflected point changed, so the hit comparison
  only remains for near mirrors (cone tangent below 0.003-0.008); the clamp range comes from every other pixel of
  a 5x5 area, and the history is clamped half way (fully clamped, the sparse tail of a highlight is cut back every
  frame; unclamped, a changed reflection ghosts for many frames).

Without `r_ssrTemporal 1` it is off (a warning), one sample per pixel would only be noise. Measurements, the
reference test and the choices of the constants: [validation](rend2-ssr-validation.md), 2026-10-29 stochastic GGX.

### Reflected fog (`r_ssrReflectedFog`, latched, default 1, froxel fog only)

The color pyramid is the scene before any fog, and the froxel fog composite (after the SSR) only fogs the camera ->
receiver part: a reflected object behind fog looked as clear as one next to the mirror. The froxel volume of
the view is built after the depth prepass, before the main pass, so its media exist when the SSR runs.
`SSRHitRadiance` (every hit radiance: the per-ray radiance pass, the direct resolve, the fused composite) marches
the segment receiver P -> hit Q through this frame's injected media (`froxelInject[current]`: sigma_s * L and
sigma_t, `froxelDynamic`, `froxelExtinction` with RGB extinction) with the Beer-Lambert steps of the integration,
one step per froxel crossed (at most 16), and returns `L * T + S`. Both ends are on the screen, so the segment
lies in the froxel frustum; past the last slice the last one is used.

- The injected light is what the media scatter towards the camera (the phase lobe of each medium); the light
  scattered into the reflection has another phase angle. Isotropic media are exact, forward scattering sun
  beams scatter a different amount into the reflection.
- Not fogged along the ray: the cubemap reflection, saber / effect reflections.
- The water surface's SSR (`r_waterSurface`, `WaterSSRTrace`) calls the same `SSRHitRadiance` and is fogged by
  the same cvar (it exists without `r_ssr`, so only `r_ssrReflectedFog` and the froxel fog gate it): its
  reflection prepass program gets `SSR_REFLECTED_FOG` (+0.04-0.13 ms on an RTX 2060 at 640x360), `u_SSRWorldToView` and the media on units of its own (25 / 13 / 27, its unit
  12 is the reflection history). Under the surface with `r_volumetricWater` the volume already holds the liquid:
  the analytic liquid path of the reflection then only covers what the volume fades out (no double extinction).
  Details and cost: `docs/rend2-water-surface.md`, "Reflected fog".
- `r_volumetricFog` other than 2 (legacy fog passes, the grid ray march): no media to march, no effect.

### Hit cache (`r_ssrHitCache 1`, off by default)

The trace reprojects the receiver into the previous frame using its velocity, reads the nearest ray sample on
the previous phased trace grid, and brings the cached hit into this view. Previous hits are decoded with the
previous projection and zFar, which can change even without a camera cut. A hit is reused when it is still
consistent with this frame's ray:

- on the ray, within the roughness cone (at least two trace pixels of angle), not beyond the ray length;
- on screen and still the visible surface there (current depth within `max(3% + 2, thickness / 4)`);
- not a moving object (the velocity at the hit leads back to where the cached hit was);
- no front surface blocks the interior of the segment to the cached hit. A conservative Hi-Z query has a
  24-iteration budget; any blocker or an untested tail invalidates the cache. There is no upper thickness
  bound that could let a much closer blocker pass.

The confidence terms are recomputed with the current geometry, and the radiance is always read from this frame's
scene: lights, sabers or a door changing color show at once. A trace cell refreshes every four frames with a
fixed phase; rotating half resolution uses independent phase and refresh cycles, covering every cell/phase
combination within 16 frames. Invalid hits trace immediately. Opaque SSR reserves the velocity image and
previous-transform UBOs even with color temporal accumulation off, keeping `r_ssrHitCache` a runtime switch.
This costs nothing: the velocity prepass uses the small `velocity` programs, while a depth-only prepass uses
the lightall family. Measured 2026-10-29 (Intel UHD, mp/ffa3, 640x360, "Depth prepass" timer): 0.10 ms
with velocity vs 0.30 ms depth only. Writing velocity only when the hit cache or temporal pass reads it was
tried and reverted: the default became 0.19 ms slower.
Without a valid depth-prepass velocity buffer, reuse is disabled. Needs a valid history (same cut rules as the temporal accumulation).
`r_ssrDebug 12` shows reused (green) and traced (red) hits.

Why validation rarely beats a fresh march, and what limits its cost:

- The validation walks the same segment, through the same Hi-Z levels, as the march to that hit. It saves the
  march's climb from level 0 and the refinement, and pays the receiver reprojection, the cached texel, the hit
  depth and the hit velocity. Cached rays shorter than 24 pixels on screen (`SSR_CACHE_MIN_PIXELS`) are traced
  again without the validation walk.
- Linear tracing (`r_ssrHiZ 0`, low preset) never reuses: without Hi-Z levels the validation steps single
  pixels with its 24-iteration budget, more than the 16-step linear march it would replace.
- The refresh slot is one per 8x8 tile of trace texels. With a per-texel 2x2 pattern every group of pixels the
  GPU runs in lockstep contained a refreshing texel, so every group paid for the full march on top of the
  validation of the others.

With these changes the cache costs less (Intel, mp/ffa3, culling off: trace 4.67/4.39 -> 3.88/3.82 ms with the
cache on), but it still does not beat tracing every ray (cache on vs off with culling: 5.00/4.73 vs 4.50/4.27 ms;
see `rend2-ssr-validation.md`, 2026-10-28). The default is therefore 0 since 2026-10-28; it stays a runtime A/B
switch. Compare `r_ssrHitCache 0/1` on the target GPU; do not infer a speedup from the green debug pixels alone.

### Confidence

Product of: hit ambiguity (ray depth vs surface depth / thickness), back facing hits (hit normal facing away from
the ray), screen edge fade (`r_ssrEdgeFade`), end of the ray, rays back towards the camera, grazing views (the ray
skims its own normal mapped surface), roughness fade, `r_ssrBlendStrength`. Misses, rays leaving the screen, sky (no
depth), the first person weapon (hacked depth range: neither receiver nor hit), and hits closer than twice the
start offset (self intersection) have 0.

The confidence says how far the SSR may replace the cubemap, not whether the ray hit something. The trace
therefore writes every hit that is not fully ambiguous, even at zero confidence: `w` holds 14 bits of
confidence, a geometry bit (always set, so `w = 0` is exactly a miss) and the reused bit
(`SSREncodeConfidence` / `SSRHitConfidence`). The resolve and the temporal confidence tests decode the
confidence; the analytic emitter occlusion and the hit cache use the geometry.

### Half resolution

One ray per 2x2 block. With `r_ssrTemporal 1` the traced pixel of the block rotates over the frames ((0,0),
(1,1), (1,0), (0,1)), so thin reflective details are not skipped for good; without it the lower left pixel. The
trace image is allocated at half size (the resolution is latched: `r_ssrHalfRes` / `r_ssrQuality` changes apply at
the next `vid_restart`). The resolve upsamples from the four nearest trace texels weighted
by bilinear position, depth similarity and normal similarity (`pow(dot, 8)`), and fetches the radiance per
sample, so reflections do not leak across silhouettes. With `r_ssrResolveCache 1`, each ray reads scene radiance
once per frame at its receiver's roughness, then the full-resolution pass filters the cached radiance with
depth, normal and roughness weights. This avoids repeating the same color-pyramid lookup for each neighbour.
It is a spatial approximation near changing roughness or ray distance: `r_ssrResolveCache 0` evaluates each
neighbour's color footprint at the full-resolution receiver instead. It never reuses lighting from an older frame.

### Temporal accumulation (`r_ssrTemporal 1`, latched)

Creates two radiance and two geometry history buffers; opaque SSR already reserves velocity for its hit cache. Each receiver is reprojected
with the velocity of the depth prepass when available (moving doors, NPCs), otherwise with the previous camera.
The history is rejected off screen, on disocclusion (previous depth vs the depth the point would have; for moving
surfaces the tolerance grows with their screen motion only), on a different normal (dot < 0.9) or roughness
(> 0.1), when the **reflected point** changed (this frame's hit against the previous hit brought into this view:
further apart in 3D than the reflection cone or two trace pixels, or a confident hit appeared / vanished - a mirror under
a moving camera keeps its depth and normal while it reflects something else), and after a cut: frame gap, map change, viewport change,
camera move > 192 units, rotation > 35 degrees, FOV change > 1 degree, or (with `r_motionBlur`) a reset of the
motion blur history (`tr.temporalHistoryValid`, which also covers `r_motionBlurReset`). The history is clamped to the YCoCg range of
the 3x3 neighborhood of the current frame, and its weight (`r_ssrTemporalWeight`) halves with large motion.
Accumulation is optional; the SSR is stable without it (no jitter, no phase rotation).
Hit lookups use the current and previous trace phases independently. The 3D test rejects a depth change even
when both hit points project to the same UV; the nearest ray is still a representative of the spatial resolve,
not an exact reconstruction of its four contributors.

## Light sabers and effects (analytic emitters)

Saber blades, blaster bolts, muzzle flashes and similar effects are additive blended surfaces: they have no
depth, are drawn after the SSR and are not in the cubemaps. They are reflected analytically instead
(`RB_SSRCollectEmitters`, `ssr_composite.glsl`, `r_ssrEmitters 1`):

- **Proxies.** Every scene entity of type `RT_SABER_GLOW` (the blade glow: `origin` to `origin + axis[0] *
  saberLength`), `RT_LINE` (the blade core, bolt tails), `RT_ORIENTEDLINE`, `RT_ELECTRICITY` (`origin` to
  `oldorigin`), `RT_CYLINDER` (radius = the larger of its two) and `RT_SPRITE` (a sphere) whose first shader stage
  is additive (destination blend ONE) becomes a capsule or a sphere. Alpha blended sprites (smoke) are skipped. As
  in a mirror, `RF_FIRST_PERSON` entities are left out and `RF_THIRD_PERSON` ones are included. At most 32, the
  brightest / largest / closest ones.
- **Color.** The "bright part" color of the stage's texture, computed once at load (`image_t::emissiveColor`, the
  average weighted by luminance * alpha, linear for sRGB textures), times the entity color (and alpha for
  SRC_ALPHA / ONE blending), times `r_ssrEmitterIntensity`.
- **Reflection.** The composite intersects the reflection ray of each receiver with the proxies (closest approach
  of the ray and the segment), with a smooth radial profile. The roughness cone of the SSR widens the proxy with
  the distance and spreads its energy (`radius / effective radius`, squared for spheres). The result is weighted
  by `W` like every other reflection and **added**: it is neither in `C` nor in the SSR scene, so nothing is
  counted twice.
- **Occlusion.** When the SSR ray of the pixel hit geometry closer than the emitter, the emitter is hidden,
  softly where the proxy reaches through the hit (`smoothstep(t - radius, t + radius + 1, hit distance)`). This
  is the geometry of the hit, not the SSR confidence: before 2026-10-28 the emitter was dimmed by
  `1 - confidence`, so a wall near the screen edge, under a grazing ray, in a ray towards the camera or on a
  surface near `r_ssrMaxRoughness` let most of a saber behind it through, and a zero-confidence hit (written as a
  miss) let all of it through. At half resolution the four nearest rays of the same surface (depth and normal
  as the upsampling weights them, phase aware) are read. A neighbour's hit `Q` is not on this pixel's ray, so
  its distance is not compared directly (since 2026-10-29): this ray is intersected with the plane of the hit
  surface when its normal is known (`Q` is a receiver), else `Q` is projected onto the ray (`t = dot(Q - O,
  R)`). The crossing must lie within `2 * gridScale` pixels (at the receiver) `+ 2 + max(cone, 0.1) * t` of
  `Q` (twice that for the plane): a neighbour whose ray went elsewhere says nothing about this one and is
  skipped. The nearest accepted crossing is kept; a same-surface miss counts as "nothing in front". When no
  neighbour is accepted, the nearest ray's projected crossing is used without the reach test (a missing saber
  reflection is the safer error than a saber through a wall). Emitters farther than `r_ssrMaxDistance` fade
  out by twice that distance.

- **Rough surfaces.** A saber also adds a dynamic light, whose specular highlight already shows on rough
  surfaces; the emitter reflection fades out between 50% and 100% of `r_ssrEmitterMaxRoughness`.
- Computed in the composite, after the temporal accumulation: fast saber swings do not smear.

## Cvars

| cvar | default | |
|---|---|---|
| `r_ssr` | 0 | archive, latch. 1 = hybrid SSR + cubemap reflections |
| `r_ssrQuality` | 1 | 0 low (16 steps, half res, linear), 1 medium (24, half res, Hi-Z), 2 high (40, full res, Hi-Z), 3 ultra (64, full res, Hi-Z) |
| `r_ssrSteps` | 0 | ray march steps (Hi-Z: iterations / 3), 0 = preset |
| `r_ssrRefineSteps` | 0 | binary search steps, 0 = preset (4, 5, 6, 8) |
| `r_ssrMaxDistance` | 1024 | max ray length, world units |
| `r_ssrThickness` | 8 | assumed surface thickness, world units (grows with distance) |
| `r_ssrMaxRoughness` | 0.6 | rougher surfaces use the cubemap only |
| `r_ssrRoughOcclusion` | 1 | latched: rough surfaces keep the cubemap's reflection occlusion (0 = old, ghosts in baked shadow) |
| `r_ssrEdgeFade` | 0.1 | screen edge fade band, fraction of the view |
| `r_ssrHalfRes` | -1 | -1 preset, 0 full, 1 half resolution rays |
| `r_ssrHiZ` | -1 | -1 preset, 0 linear, 1 hierarchical |
| `r_ssrTemporal` | 0 | archive, latch. temporal accumulation |
| `r_ssrTemporalWeight` | 0.9 | history weight |
| `r_ssrBlendStrength` | 1 | confidence scale. 0 = cubemap only (and no SSR work unless a debug view or the compare is on) |
| `r_ssrCompare` | 0 | split screen: left half cubemap reflections only, right half hybrid |
| `r_ssrHitCache` | 0 | reuse validated hits (Hi-Z only); refresh every 4 frames per 8x8 tile, all rotating half-res phases within 16 frames; needs depth prepass. Off: measured slower than tracing every ray |

| `r_ssrResolveCache` | 1 | resolve each half-res ray's current-frame radiance once before edge-aware upsample; 0 = direct per-pixel footprint |
| `r_ssrReceiverCull` | -1 | 1 = early depth classification of the pixels that need a ray, 0 = the trace discards the others itself, -1 = classify only while under ~15% of the trace texels need a ray (measured share, hysteresis 12% / 18%) |
| `r_ssrFusedResolve` | 1 | without temporal accumulation the composite resolves the rays itself (no full resolution resolve image); 0 = separate pass |
| `r_ssrFootprintDepth` | 1 | rough reflections do not blur surfaces in front of the hit into it (Hi-Z tracing only) |
| `r_ssrReflectedFog` | 1 | archive, latch: fog the SSR along the reflected ray with the froxel fog (`r_volumetricFog 2` only; see Reflected fog). Intel 640x360: +0.07 ms half res, +0.4-0.8 ms full res |
| `r_ssrStochastic` | 0 | archive, latch, needs `r_ssrTemporal 1`: GGX sampled rays reused by the neighbours instead of the mirror ray with a cone blur (see Stochastic GGX; the SSR costs about 1.6-1.7x in game on Intel) |
| `r_ssrDebug` | 0 | cheat, see below |
| `r_ssrEmitters` | 1 | reflect light sabers and additive effects (analytic) |
| `r_ssrEmitterIntensity` | 1 | brightness of those reflections |
| `r_ssrEmitterMaxRoughness` | 0.35 | rougher surfaces do not reflect them |

## Debug views (`r_ssrDebug`)

Non-receiver pixels are black. 1-6 are drawn over the final image without tone mapping, 7-11 replace the scene
color of the view before the rest of the pass and go through the normal tone mapping.

| | |
|---|---|
| 1 | material normal (world, `* 0.5 + 0.5`) |
| 2 | roughness |
| 3 | specular reflectance W (F0 * EnvBRDF.x + EnvBRDF.y with specular occlusion) |
| 4 | ray hit (green, brighter = more confident; olive = geometry at zero confidence, an emitter occluder only) / miss (red); non-receivers dark gray |
| 5 | hit distance (blue near, red at `r_ssrMaxDistance`) |
| 6 | final confidence (after temporal accumulation) |
| 7 | raw SSR radiance |
| 8 | cubemap reflection C |
| 9 | final hybrid reflection `C + c * (SSR * W - C)` |
| 10 | replaced part `abs(c * (SSR * W - C))` |
| 11 | light saber / effect reflections alone (9 includes them) |
| 12 | hit cache: reused hit (green), traced hit (red), miss (dark) |

A/B: `r_ssr 0` is the previous renderer; `r_ssrCompare 1` shows cubemap-only and hybrid side by side in the same
frame; `r_ssrDebug 8` vs `9` shows the reflection term alone. `build/ab/*-pressr.dll` (HEAD) vs `*-ssr.dll`.

GPU timers: `r_speeds 100` lists "Screen geometry" (shared MSAA resolve + depth pyramid), "SSR inputs" (color
pyramid), "SSR trace", "SSR resolve", "SSR temporal", "SSR composite". With the fused resolve, "SSR resolve" is
only the per-ray radiance pass (or nothing at full resolution) and the resolve work moves into "SSR composite".

## GPU cost

The cache-fix build, GPU regressions and isolated game-run procedure are recorded in
[`rend2-ssr-validation.md`](rend2-ssr-validation.md). Use `r_speeds 100` for per-pass times on the target
scene and GPU. Compare `r_ssrHitCache`, `r_ssrReceiverCull` and `r_ssrResolveCache` separately; cache
validation and extra passes can cost more than the work they replace in cheap or dense scenes.

Water surface SSR (`r_waterSurface`, its own reduced reflection prepass with the trace settings of this page):
in-game per-sub-pass timings and the `r_waterSSROptimize` savings are in `docs/rend2-water-surface.md`
("Water SSR cost in game"). The march itself was ~1 ms of it on Intel UHD; the second rasterization of the
water was the cost.

## Validation checklist

The cache-fix renderer builds in MSVC SP/MP. Its 17 SSR shader permutations and targeted regressions run
on Intel UHD and RTX 2060 (`tools/rend2/test_ssr_gl.py`, including the 2026-10-28 emitter occlusion, fused
composite, footprint depth, cache length gate and LESS classification checks); the shared water library also
passes the focused RTX motion/SSR compilation checks. See the validation report for game runs and remaining coverage. The broader visual checklist
(`r_ssr 1`, `r_ssrCompare 1`, `r_ssrDebug 4/6/9/10`) remains:

- polished metal (sharp, tinted by F0), rough metal (blurred, fades to the cubemap), painted metal and rough
  dielectrics (weak), glossy dielectric floors (strong at grazing angles only)
- characters reflected in floors; moving doors and NPCs (with and without `r_ssrTemporal`)
- saber / blaster over a polished floor (`r_ssrDebug 11`): sharp blade line on mirrors, wider and dimmer on
  glossy surfaces, gone on rough ones; saber behind a wall (hidden when the wall is on screen)
- screen edges (fade, no hard cut), long corridors (Hi-Z vs linear, `r_ssrMaxDistance`)
- vjun / wet looking maps: no double reflection where the confidence is high (`r_ssrDebug 10` vs `8`)
- MSAA on/off, `r_hdr` on/off, `r_cubeMapping` on/off, different resolutions / FOV, weapon in view (no SSR on it)

## Known limitations

- Screen space: only what is on screen and in the opaque part of the pass can be reflected. Glass, smoke and other
  alpha blended surfaces are not in the SSR scene and are not hit (no depth); they fall back to the cubemap.
  Off-screen and occluded geometry too.
- Emitters: only effect entities (sabers, lines, cylinders, sprites) with an additive first stage, not additive
  world surfaces, poly effects or oriented quads; shape and color are approximations (a capsule with the average
  bright color of the texture). Their occlusion is only known on screen: an emitter behind an off-screen wall can
  leak into a reflection. Rough surfaces show the saber's dynamic light highlight instead.
- Fog along the reflected ray only with the froxel fog (`r_ssrReflectedFog`, see Reflected fog): with the
  legacy fog passes only the fog of the receiver is applied and reflected objects behind fog look too clear. The
  froxel in-scattering keeps the phase angle towards the camera.
- A multiplicative stage drawn after the PBR stage of the same shader (rare) is not included in `C`.
- The depth buffer only has the front faces: thin objects are thickened by `r_ssrThickness`, which can produce
  false hits behind them (reduced by the ambiguity term and the back facing test).
- `r_hdr 0`: the scene buffer is 8 bit, the composite clamps (subtract pass, then add pass).
- The first person weapon is detected by its depth range (hardware depth <= 0.3). World geometry only lands there
  closer than ~1.43 x `r_znear` (a few units), and would then just not be a receiver or hit. A weapon drawn
  without `RF_DEPTHHACK` would be a normal receiver/hit target. Its color is excluded from every color-pyramid
  level, including sharp mip 0 and bilinear samples along its boundary.
- Only the main view of a scene gets SSR (not portals, mirrors, sky portals).
- MSAA: the attachments are averaged per pixel while the depth is one sample; a representative-sample resolve
  would need multisample textures (the attachments are renderbuffers). Silhouette pixels mostly fail the depth
  validation and keep the cubemap.
- Rough reflections are a mirror ray plus a cone blur of the scene (cheap, matches the cubemap prefilter), not a
  distribution of rays: occlusion and parallax inside a rough lobe are those of the mirror direction. The blur
  is kept off nearer surfaces (`r_ssrFootprintDepth`), not off farther ones. `r_ssrStochastic 1` traces the lobe
  instead; it relies on the temporal pass, so a reflection that changes (a lamp switching, an NPC walking past)
  converges over a few more frames than the cone, and the reflections of moving objects on rough surfaces are
  reprojected with the receiver's motion, not the hit's.

## Possible improvements (not implemented)

- Stochastic GGX (`r_ssrStochastic`): a history of luminance moments (variance guided clamping, SVGF) instead of
  the half clamp, and reprojection by the hit point for rough reflections of moving objects.
- Reflected fog: the in-scattering with the phase angle of the reflected ray (a per-medium phase lookup during
  the march), the cubemap reflection. SSR for remaining transparent surfaces such as
  glass (water already has its own pass).
- A screen-space emissive layer for the remaining additive effects (exact look, on screen only), next to the
  analytic emitters.
- Pack the material buffer tighter (R11G11B10F for `C`), or reuse the prepass for the normal.
- Depth pyramid shared with GTAO (GTAO uses farthest-weighted averages built before the main pass, a different
  filter).
- GL 4.3 compute path: 8x8 tile classification, a compacted SSBO list of the rays, an indirect dispatch over it
  (the early-depth classification stays the GL 3 fallback); a single-dispatch color / Hi-Z pyramid. The
  compaction only pays where the receivers are sparse; measure against the fixed fragment cull first.
- Rough reflections: specular occlusion from the GTAO bent cone around the reflection direction instead of the
  lightmap luminance proxy of `r_ssrRoughOcclusion`.
- Representative-sample MSAA resolve of the material attachments (needs multisample textures).

- Screen-space specular occlusion from the trace for the cubemap part that remains.
