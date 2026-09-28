# Rend2: screen-space ambient occlusion and contact shadows

Rend2 is a forward renderer with a depth prepass. Screen-space AO and contact shadows are computed right after
the depth prepass of a world view, in fullscreen fragment passes (GLSL 1.50, no compute shaders), and the main
pass (`lightall.glsl`) of the same view samples the result through `u_SSAOMap` (`TB_SSAOMAP`):

- `r` = ambient occlusion (visibility, 1 = unoccluded)
- `g` = sun contact shadow visibility (1 = lit)
- `ba` = octahedral world space GTAO bent normal (`r_gtaoBentNormals`, indirect light only)

Code: `shared/rd-rend2/tr_ao.cpp`, shaders `gtao_depth.glsl`, `gtao.glsl`, `gtao_denoise.glsl`,
`ao_composite.glsl`, `ao_debug.glsl`; application in `lightall.glsl`.

Everything is off by default (`r_ssao 0`, `r_aoMode -1`, `r_contactShadows 0`): no resources are created, no
shader permutation changes, the image is unchanged.

## Render pass order (per world view)

1. Depth prepass (`RB_RenderDepthOnly`). With MSAA the main view depth is now resolved into
   `tr.renderDepthImage` before AO runs (the old SSAO read the previous frame's depth with MSAA).
2. `RB_RenderScreenSpaceLighting` (tr_ao.cpp), only for views rendered into `tr.renderFbo` with a plain
   perspective projection: not for sky portals, mirrors/portals (oblique near plane), cubemap or shadow views.
   - legacy SSAO (`r_aoMode 1`): `renderDepth -> hdrDepth -> ssao -> depthBlur x2 -> screenSsao`, unchanged
   - GTAO (`r_aoMode 2`):
     1. `gtao_depth` LINEARIZE: `renderDepth -> aoDepth` mip 0 (linear view depth, half or full resolution)
     2. `gtao_depth`: `aoDepth` mips 1..3 (farthest-weighted average)
     3. `gtao`: main pass `-> gtao[0]` (visibility + reconstructed normal), `BENT_NORMAL` permutation also
        `-> gtaoBent[0]` (second color attachment)
     4. `gtao_denoise` x `r_gtaoDenoise`, ping-pong `gtao[0] <-> gtao[1]` (and `gtaoBent[0] <-> gtaoBent[1]`)
   - `ao_composite` (full resolution) `-> screenAo`: AO (legacy bilinear / GTAO depth-aware upsampled /
     split screen) and the contact shadow ray march. Skipped for plain legacy SSAO without contact shadows:
     lightall then samples `screenSsao` directly, exactly as before.
3. Main pass: lightall applies AO and contact shadows.
4. Post processing; `r_aoDebug` overlays are drawn at its end.

Views without a result (no depth prepass, sky portal, ...) bind the white image (previously they could read a
stale SSAO map of another view).

## GTAO

Horizon-based ambient occlusion after Jimenez et al. 2016 ("Practical Real-Time Strategies for Accurate Indirect
Occlusion"), structured like Intel's XeGTAO (MIT license, notice kept in `gtao.glsl`) but written for this
renderer, not a port:

- **Input**: the depth buffer and the projection matrix (`P[0], P[5], P[8], P[9]` for positions,
  `P[10], P[14]` for linear depth). View space: x right, y up, z = distance along the view direction.
- **Depth prefilter**: linear depth at half resolution picks the closest or the farthest of the 2x2 source
  depths in a checkerboard, so both sides of silhouettes survive for upsampling. Three more mips are built with
  a weighted average that prefers the farthest depth; distant samples read coarser mips (XeGTAO's
  `log2(offset) - 3.3`), which keeps large radii cache friendly and makes thin foreground objects fade out of the
  coarse levels instead of producing halos. First person weapon pixels (`RF_DEPTHHACK`, depth <= 0.3) are marked
  invalid: they get no AO and never occlude.
- **Normals**: reconstructed from depth. For each axis the neighbour whose depth is best predicted by linear
  extrapolation of the next pixel is used, so the derivative never spans a depth discontinuity. Only
  `ReconstructNormal()` in `gtao.glsl` would change to use real surface normals later.
- **Main pass**: for each pixel, `slices` directions spread over 180 degrees; along both sides of each direction
  `steps` depth samples with a quadratic distribution out to the projected effect radius. The maximum horizon
  cosine per side (with a distance falloff towards the radius, and optional thin-occluder compensation) gives
  two horizon angles, clamped to the hemisphere around the normal projected into the slice, and the
  cosine-weighted visibility of the slice is integrated analytically. Visibility = average over slices, then
  `pow(visibility, r_gtaoPower)`.
- **Noise**: a fixed 4x4 ordered pattern rotates the slices and offsets the steps. Every 4x4 block contains all
  rotations and the denoiser covers exactly that footprint, so there is no temporal shimmer without TAA.
- **Denoise**: 3x3 edge-aware passes with tap distance 1, 2, 4. Weights: binomial kernel x distance of the
  neighbour to the center's tangent plane (sloped floors blur fully, steps and silhouettes do not) x normal
  similarity. Sky and weapon pixels are skipped.
- **Upsampling** (half resolution): the 2x2 bilinear footprint of each full resolution pixel, each texel weighted
  by how well its tangent plane predicts the pixel's position; falls back to the best matching texel on edges.
  Never a plain bilinear blur across geometry edges. No temporal accumulation, so moving characters leave no
  trail, and a character in front of a wall does not blend its AO into the wall's (plane distance rejects it).
- **Bent normals** (`r_gtaoBentNormals`, default 1, GTAO mode only): the per slice cosine weighted mean of the
  unoccluded arc (XeGTAO's "Algorithm 2" extension: `t0`, `t1` from the two horizon angles and the projected
  normal angle, rotated from the slice frame to view space, weighted by the projected normal length). The AO
  algorithm itself is unchanged. Stored as an octahedral view space normal in `RG8` (z flipped: bent normals face
  the camera, so they stay in the unfolded half), denoised with the AO weights (decoded, averaged as vectors,
  renormalized), upsampled with the AO weights, rotated to world space and written octahedrally into `screenAo.ba`.

## Contact shadows

In `ao_composite.glsl`, for the sun only (`r_sunlightMode 1/2`, map with sun shadows):

1. view position from the full resolution depth;
2. the sun direction in view space;
3. the ray starts off the receiver by 1.5 pixel footprints (+0.1 units) to avoid self-shadowing;
4. `r_contactShadowSteps` samples over `r_contactShadowLength` units at fixed step midpoints (a screen-locked
   jitter crawled during camera motion); `r_contactShadowSoft 1` packs them towards the receiver instead;
5. each sample is projected to the screen and compared with the depth buffer;
6. a hit needs the ray to be behind the depth buffer by more than a bias (1.5 pixel footprints) and less than
   `r_contactShadowThickness` (+2 pixel footprints): farther behind means the ray passes behind a foreground
   object, not through an occluder;
7. the first hit gives the occlusion, faded out over the second half of the ray; rays leaving the view stop
   (unknown occluders), and occlusion fades out within 5% of the screen edges.

lightall: `sunShadow = cascadeShadow * contactShadow * N.L`. With `r_sunlightMode 1` this modulates the
lightmap like the cascaded shadows already do, with mode 2 it scales the real-time sun. Contact shadows only
add near-field detail; they do not replace the shadow map.

## AO application (lightall)

AO represents lost indirect light. Two applications are available (`r_aoApply`):

| | legacy (0) | indirect-only (1) |
|---|---|---|
| ambient term (entities, light left over from lightmaps/vertex light) | x AO | x AO (multi-bounce) |
| baked lightmap / vertex light | - | x `mix(1, AO, r_aoLightmapFraction)` |
| cubemap reflections | F0 term x AO | whole IBL term x specular occlusion (`r_aoSpecOcclusion`) |
| cubemap brightness normalization | from AO'd lighting | from unoccluded lighting |
| SSR hits | weight with F0 x AO | not occluded (traced geometry); the cubemap they replace is |
| diffuse IBL direction (`r_diffuseIBL`, entities) | N | N bent by GTAO (`r_gtaoBentNormals`) |
| sun, dynamic lights, light grid directed light, contact shadows | - | - |
| material AO (ORM texture) | min(material, screen) | min(material, screen) |

Default `-1`: legacy for legacy SSAO (bit exact with the old output), indirect-only for GTAO. JKA lightmaps
contain direct and bounced light together, so AO attenuates only a share of them (`r_aoLightmapFraction`),
otherwise GTAO would be invisible on world surfaces or act like a shadow.

Multi-bounce (`r_aoMultiBounce`, Jimenez 2016 fit): bright and colored surfaces keep more light in creases
instead of turning dirty black.

Diffuse indirect: AO multiplies the ambient term before the diffuse IBL `directionalFactor` shapes it, so the
directional diffuse IBL is attenuated like the rest of the ambient light. AO is never applied after tone mapping
(the old full screen multiply in `tr_backend.cpp` stays `#if 0`).

Fixed with the indirect-only application (the legacy application keeps the old behaviour bit exact for A/B):

- the old specular occlusion only scaled `F0 * EnvBRDF.x`; the Fresnel term `EnvBRDF.y`, which dominates
  dielectric reflections at grazing angles, and the whole cloth BRDF reflection were never occluded;
- the cubemap brightness was normalized by the luminance of the already AO'd lighting, occluding reflections a
  second time, the same way as diffuse ambient;
- SSR hits were occluded although they are traced geometry.

### Specular occlusion (`r_aoSpecOcclusion`)

Applied to the whole cubemap reflection (`CalcIBLContribution(...) * specOcclusion`). `ao = min(material AO,
screen AO)`, `alpha = roughness^2`, `R = reflect(-V, N)`:

| Value | Formula |
|---|---|
| 0 | `ao` (scalar, `specularIBL *= AO`) |
| 1 | Lagarde & de Rousiers 2014: `saturate(pow(N.V + ao, exp2(-16 alpha - 1)) - 1 + ao)` |
| 2 | cone / cone (Jimenez 2016): `cap(cosV, cosS, B.R) / cap(0, cosS, N.R)` |
| -1 (default) | 2 with GTAO bent normals, otherwise 1 |

Mode 2: the visibility cone around the bent normal `B` has `cosV = sqrt(1 - ao)` (cosine weighted AO), the
specular lobe cone around `R` has `cosS = 10^-alpha^2` (as in Unity HDRP), `cap()` is the spherical cap
intersection solid angle (Oat & Sander 2007 smoothstep approximation). Dividing by the lobe part above the surface
(a hemisphere around N) makes an unoccluded surface exactly 1 at any angle, since `EnvBRDF` already accounts for
the part of the lobe below the horizon. A mirror stays fully lit while `R` points into the unoccluded cone and loses
its reflection only where `R` points into the occluder. A rough lobe covers the whole cone and converges to about
`ao`. N.V enters through `R`. Without a bent normal, `B = N`.

### Bent normals in lightall

`indirectN = normalize(N + w * (bent - geometricNormal))`, `w = r_gtaoBentNormals * saturate(2 * (1 - screenAO))`:
the offset GTAO found relative to the geometry is added to the normal mapped shading normal, only where there
is occlusion. Views without GTAO sample the white image (AO = 1, so w = 0). `indirectN` is used only for the diffuse
irradiance probe lookup and the specular occlusion cone, never for the sun, dynamic lights or the direct BRDF.

## Cvars

| Cvar | Default | Description |
|---|---|---|
| `r_aoMode` | -1 | -1 = follow `r_ssao` (legacy), 0 = off, 1 = legacy SSAO, 2 = GTAO |
| `r_ssao` | 0 | legacy switch (latched), unchanged; `r_ssao 2` still shows its old debug overlay |
| `r_aoApply` | -1 | -1 auto, 0 legacy application, 1 indirect-only |
| `r_aoCompare` | 0 | cheat. Split screen: legacy SSAO + legacy application left, GTAO + indirect-only right |
| `r_aoMultiBounce` | 1 | multi-bounce approximation (indirect-only application) |
| `r_aoLightmapFraction` | 0.5 | share of baked light attenuated by AO (indirect-only application) |
| `r_aoSpecOcclusion` | -1 | specular occlusion (indirect-only application): -1 auto, 0 scalar, 1 Lagarde, 2 cone |
| `r_gtaoQuality` | 2 | preset, see below |
| `r_gtaoHalfRes` | 1 | latched. 1 = half resolution + depth-aware upsampling, 0 = full resolution |
| `r_gtaoRadius` | 32 | effect radius, world units |
| `r_gtaoFalloff` | 0.6 | falloff range as a fraction of the radius |
| `r_gtaoThickness` | 0.2 | thin occluder compensation (0 = solid occluders, higher = fewer halos behind objects) |
| `r_gtaoPower` | 1.5 | visibility exponent |
| `r_gtaoDenoise` | 2 | denoise passes, 0-3 |
| `r_gtaoBentNormals` | 1 | bent normal strength for indirect light, 0 = off (no bent normal passes) |
| `r_contactShadows` | 0 | sun contact shadows |
| `r_contactShadowLength` | 16 | ray length, world units |
| `r_contactShadowSteps` | 12 | ray steps |
| `r_contactShadowThickness` | 6 | assumed occluder thickness, world units |
| `r_contactShadowStrength` | 0.85 | 0..1 |
| `r_contactShadowSoft` | 0 | 1 = soft depth-weighted hits, steps packed near the receiver |
| `r_aoDebug` | 0 | cheat, debug views, see below |

GPU resources (and the `USE_SSAO` define in the shaders) exist when `r_ssao`, `r_aoMode > 0` or
`r_contactShadows` is set when the renderer starts; switching between modes afterwards is immediate, turning the
feature on from nothing needs `vid_restart` (a warning is printed). All of it needs `r_depthPrepass 1`.

Quality presets (`r_gtaoQuality`), depth taps per pixel = slices x steps x 2:

| Value | Slices | Steps per side | Taps |
|---|---|---|---|
| 0 low | 1 | 3 | 6 |
| 1 medium | 2 | 4 | 16 |
| 2 high (default) | 3 | 6 | 36 |
| 3 ultra | 6 | 8 | 96 |

## New textures and FBOs

| Image | Format | Size | FBO |
|---|---|---|---|
| `*screenSsao` (legacy, was `GL_R8`) | `GL_RG8`, r = AO, g = 1 | 1/2 | `_screenssao` |
| `*aoDepth` | `GL_R32F`, 4 mips | 1/2 or 1 | `_aoDepth0..3` (one per mip) |
| `*gtao0`, `*gtao1` | `GL_RGBA8`, r = visibility, gba = normal | 1/2 or 1 | `_gtao0`, `_gtao1` (color 0) |
| `*gtaoBent0`, `*gtaoBent1` | `GL_RG8`, octahedral view space bent normal | 1/2 or 1 | `_gtao0`, `_gtao1` (color 1) |
| `*screenAo` | `GL_RGBA8`, r = AO, g = contact, ba = octahedral world bent normal | 1 | `_screenAo` |

`*hdrDepth` / `_hdrDepth` and the `_quarter` FBOs are used by legacy SSAO as before.

## Debug views (`r_aoDebug`, cheat)

| Value | View |
|---|---|
| 1 | raw legacy SSAO (computed even in GTAO mode) |
| 2 | raw GTAO (denoiser skipped) |
| 3 | denoised GTAO (before upsampling) |
| 4 | reconstructed view normals |
| 5 | linear depth (log scale; weapon red, sky blue) |
| 6 | contact shadows only |
| 7 | cascade shadow only (lightall) |
| 8 | combined sun visibility: cascade x contact x POM self shadow, without N.L (lightall) |
| 9 | diffuse ambient visibility incl. material AO and multi-bounce (lightall) |
| 10 | the AO map lighting used |
| 11 | world space bent normal (x 0.5 + 0.5) |
| 12 | specular occlusion of the cubemap reflection (lightall) |

Views 7-9 and 12 are written by lightall on lit surfaces and skip tone mapping, bloom and sun rays; other surfaces
(sky, effects) keep their normal shading.

## A/B comparison

```
r_aoMode 1; r_aoApply -1        // legacy SSAO, legacy application (identical to before)
r_aoMode 2                      // GTAO
r_aoCompare 1                   // split screen legacy | GTAO
r_aoMode 2; r_aoApply 0         // GTAO with the old application
r_contactShadows 1              // toggle contact shadows (0/1)
r_aoDebug 2 / 3 / 11 / 9 / 12 / 6 / 8   // raw, denoised, bent normal, diffuse visibility,
                                // specular occlusion, contact, sun visibility
r_aoSpecOcclusion 0 / 1 / 2     // scalar / Lagarde / cone specular occlusion
r_gtaoBentNormals 0 / 1         // bent normals off / on
r_speeds 100                    // GPU timings: "AO legacy SSAO", "AO GTAO depth", "AO GTAO main",
                                // "AO GTAO denoise", "AO composite/contact"
```

Start with `r_aoMode 2` (or `r_contactShadows 1`) in the config, or `vid_restart` after setting it.

## Screen-space limitations

- Only what is visible in the depth buffer occludes: off-screen or hidden occluders, back faces and the inside of
  thin objects are unknown (AO and contact shadows fade out at screen edges instead of popping).
- Depth has no thickness: thin objects in front of a surface occlude as if they were solid
  (`r_gtaoThickness`, contact thickness limit mitigate this).
- Transparent / non-depth-writing surfaces (glass, effects, most foliage cards with alpha blending) neither
  receive nor cast AO/contact shadows.
- No AO in mirrors/portals and sky portals, cubemap captures, or for the first person weapon.
- Contact shadows: primary sun only; not for dynamic/point lights; the default result is binary per pixel
  (hard, aliased edges up close); `r_contactShadowSoft 1` weights hits by depth instead, see
  [rend2-character-shadows.md](rend2-character-shadows.md).
- Half resolution GTAO loses detail on sub-2-pixel features; use `r_gtaoHalfRes 0` for them.
- Lightmaps already contain large scale occlusion; `r_aoLightmapFraction` is an artistic, not a physical,
  split between direct and bounced baked light.

## Possible improvements (not implemented)

- Temporal accumulation (the velocity buffer exists for `r_smaa 2`): fewer taps per frame, stable 16 x 4x4
  rotations over time.
- True normals from the depth prepass (an extra RG16 target) instead of depth reconstruction.
- Contact shadows for the strongest dynamic light; per-light screen-space shadows.
- A 5-level depth mip chain with a separate pass for very large radii, and a quality preset per resolution.
- Bent normals for lightmapped world surfaces (they have no directional ambient lookup to steer yet).
- AO for the ambient part of `generic.glsl` vertex-lit surfaces.
