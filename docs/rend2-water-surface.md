# Modern water surface (r_waterSurface)

Water-body identity and dynamics metadata are documented in
[`rend2-water-bodies.md`](rend2-water-bodies.md). The optional per-body
ambient-wave layer is documented in [`rend2-water-waves.md`](rend2-water-waves.md).
World-caused body-local disturbances are documented in
[`rend2-water-interaction.md`](rend2-water-interaction.md).

A physically based surface for the stock water of the maps: dielectric Fresnel reflection (SSR -> cubemap ->
fallback), depth aware screen-space refraction, absorption / scattering of the liquid along the refracted path,
GGX glints of the sun and the dynamic lights. No map is rebuilt, no asset changes: the water is found from the
BSP semantics. The generic refraction (`refractive` shaders, `RF_DISTORTION`) is not touched.

Code: `shared/rd-rend2/tr_watersurface.cpp`, `glsl/watersurface.glsl`; hooks in `tr_shade.cpp`
(`RB_IterateStagesWater`), `tr_backend.cpp` (`RB_SubmitRenderPass` water slot, depth prepass), `tr_bsp.cpp`
(`R_WaterClassifySurfaces`), `tr_image.cpp` / `tr_fbo.cpp` (targets), `tr_glsl.cpp` (program).
Tools: `tools/rend2/water_audit.py` (pk3 audit + classification coverage), `tools/rend2/test_watersurface_gl.py`
(GPU checks, `--bench` timings).

## The legacy path (before this change)

- `shader_t::useDistortion` is set only by the rend2 keyword `refractive` (`ParseShader`); `tr.distortionShader`
  and entities with `RF_DISTORTION` (force push, effects) take the same path. `surfaceparm water` does not.
- `RB_RenderDrawSurfList` skips distortion shaders in the main pass; after `RB_PostProcess` has tone mapped the
  frame into the back buffer, a second pass (`backEnd.refractionFill`) draws them with `refraction.glsl`.
- That program reads `renderFbo` color (the HDR scene, MSAA resolved, *after* every transparent surface) and
  applies the output transform (tone map, LUT) itself; it writes LDR into the back buffer, depth tested against
  the blitted scene depth.
- `u_ScreenDepthMap` is bound (`renderDepthImage`) but never sampled: the refracted position is the vertex moved
  by a constant `distance = u_Color.a` (10 units, scaled by the tcMod) along `refract(view, normal, 1/1.3)`. No
  thickness: a puddle and a lake refract the same, nothing absorbs.
- **No stock water shader is `refractive`** (audit below): the stock water is drawn by its blended legacy stages
  (scrolling `water3`, `$lightmap` modulation, additive `stars`), sorted with the other transparent surfaces.
- All `CONTENTS_WATER` surfaces are non-refractive; `r_volumetricWater` (tr_liquid.cpp) already treats their
  brushes as a froxel medium: the composite fogs everything behind the water from the depth buffer (the water
  surface does not write depth), lightall attenuates the sun under water (`USE_LIQUID_SUN`, caustics), cgame's
  legacy tint is skipped for the classes it handles.

## Reference approaches

- Unreal Single Layer Water: a custom pass after the base pass and deferred lighting, before translucency; the
  surface reads scene color and depth and shades a homogeneous participating medium (absorption, scattering, phase)
  behind an opaque surface BRDF, with SSR / reflection captures ([docs](https://dev.epicgames.com/documentation/unreal-engine/single-layer-water-shading-model-in-unreal-engine)).
- CryEngine water volume: surface reflection with a Fresnel term over refraction, the underwater look from the
  volume's fog density / color, generated detail normals ([Water Volume](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/36869912),
  [WaterVolume shader](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/29449415)).

The rend2 adaptation keeps the main pass architecture (sorted draw items): the classified surfaces get one draw of
the water program instead of their stages, and those items are moved to their own slot of the pass.

## Pipeline (r_waterSurface 1)

```
depth prepass (water excluded) -> shadows / AO / froxel build
main pass:  opaque sort -> screen space (SSGI, SSR: Hi-Z + opaque color pyramid)
            -> decals .. SS_FOG layers
            -> WATER SLOT: copy the conservative visible-water union rect of
               renderFbo color + glow + depth (MSAA resolved; full-view fallback)
                          -> reduced water SSR + temporal history -> water draws (replace, write depth)
            -> atmosphere -> clouds -> froxel fog composite   (fog camera -> water surface)
            -> SS_UNDERWATER, blended layers                  (depth tested against the water)
post: bloom (sees the glints), tone map, refraction pass of the refractive shaders (unchanged)
```

- The final water program is drawn once per interface surface (`RB_IterateStagesWater`), its items tagged
  (`RenderState::waterSurface`); `RB_SubmitRenderPass` takes them out of the sorted order and draws them in the slot.
  A legacy fog pass of a water surface follows it in the slot (froxel views: the composite fogs it instead).
- A refractive shader classified as water (on a liquid brush) is drawn in the main pass by the water program,
  not by the refraction pass (`RB_WaterSurfaceDistortion`).
- `r_waterSurface 0` (default, latched): no targets, no programs, no extra pass, `RB_SubmitRenderPass` and the
  stage iterator take their old branches; only the cached per map classification runs, for `r_waterInfo`.

## Classification

Per map (`R_WaterClassifySurfaces`, after the surfaces are loaded):

1. BSP semantics: a drawn surface whose BSP shader has `CONTENTS_WATER` (`surfaceparm water`) is a candidate;
   lava contents never are; `CONTENTS_SLIME` gives slime optics.
2. The liquid brush it lies on: all vertices must be inside a brush of the **same BSP model** and on
   **one common plane**. Model indices and brush bounds reject unrelated brushes before plane tests.
   Fog-medium and optics are attached to each surface, rather than every use of its shader.
3. Each upward surface is classified independently. Only pool/lake interfaces use the new program, with
   two-sided culling so the same interface remains visible underwater. Bottoms, sides and waterfalls retain
   their stages even when they share a shader with the top. The surface key is part of draw/merge batching.
4. An upward boundary linked to a water/slime brush is an interface, including refractive materials.
   Other refractive surfaces keep generic refraction. Optics follow a matching env.json `"Liquids"` profile
   and explicit overrides. Froxel coverage is used only for a linked, supported world brush of this surface.
5. `r_waterOverride <shader | prefix*> on [water | slime] | off | clear` (cfg friendly, the last matching line wins,
   applied to the current and later maps).
6. Last, `r_waterSurfaceExperimental 1`: water-like name (whole words: water, pool, lake, river, pond, ocean,
   swamp, liquid), mostly facing up, no water contents. Off by default; never a plain substring.

`r_waterInfo [surfaces]`: liquid brushes (model, class, sides, bounds, fog), every candidate shader with its decision,
counts up/down/vertical/sloped, up share, brush links, contents, refractive, and the reason; `surfaces` lists every
candidate surface with its orientation, normal, area and brush side.

The surface itself decides per pixel which side it is seen from: liquid brush faces point out of the liquid, so a
camera behind the face is inside the liquid (Snell's window, total internal reflection). A face seen from inside with
the scene right behind it (bottom / side against the floor or a wall) is passed through, not an air interface.

## Shading (glsl/watersurface.glsl)

- Waves: a generated tiling slope texture (integer wave vectors, `R_WaterBuildWaveSlopes`, RGBA16F
  `(sx, sy, sx^2, sy^2)` with mips): two world layers drifting (wrapped on the CPU) and one layer on the first textured
  stage's coordinates with its tcMods (the scroll of the legacy water is its flow). The mips keep the slope variance:
  distant waves turn into roughness (LEAN), no aliasing. Shader `deformVertexes` run in the vertex shader.
- Fresnel: specular IBL weight `W = F0 EnvBRDF.x + EnvBRDF.y`, F0 of the IOR (0.02 for 1.333); without the
  EnvBRDF LUT (`r_cubeMapping 0`) the exact dielectric Fresnel; from inside the exact Fresnel with TIR.
- Reflection: SSR first (the `ssr_common.glsl` march over the view's Hi-Z / opaque color pyramid, the trace settings of
  `r_ssrQuality` / `r_ssr*`, Hi-Z walk when the SSR uses it, the confidence terms of `ssr_trace.glsl`, the cone mip of
  `ssr_resolve.glsl`), blended with `r_waterSurfaceSSR` over the parallax corrected cubemap of the surface, else the sky
  ambient (`sunAmbCol`). Water shares `SSRHitRadiance` and the march library. Water requests the Hi-Z and
  color pyramid independently of opaque-material `r_ssr`; with `r_ssr 0` the opaque trace/composite is skipped.
  Rays use `r_ssrHalfRes` / the quality preset (medium: half resolution), then temporal history validated against
  receiver depth, normal, roughness and world hit position. A four-tap bilateral resolve reconstructs the final
  reflection. Missing surface probes use the nearest valid camera cubemap before the ambient fallback.
  Views with no submitted interfaces do not request water SSR work. With opaque SSR disabled, its material attachments, trace/resolve/history targets and GPU programs are not allocated.
- Refraction: the refracted ray down to the depth of the scene behind the water: first guess from the depth straight
  behind the pixel (bounded), two fixed point steps with the depth found at the sample; the offset scaled by
  `r_waterSurfaceRefraction`, bounded to 12% of the screen. Depth rejection (`r_waterSurfaceDepthReject`): a sample in
  front of the surface (foreground) halves the offset up to three times, else no refraction. Sky behind: the longest path.
- Medium: path length = depth below the surface plane of the refracted scene point / cos(theta_t), times
  `r_waterSurfaceDepthScale`. Optics: the `r_volumetricWater*` / `r_volumetricSlime*` cvars through
  `R_LiquidsMaterial` (the same values as the froxel medium, whether `r_volumetricWater` is on or not), extinction
  scaled by `r_waterSurfaceAbsorption`. Where the froxel volume holds the liquid (r_volumetricWater class of a world
  brush, or a fog volume brush) the segment surface -> scene is taken from it (`FroxelFog` ratio, scalar or RGB),
  fading to the analytic medium over its last fifth; elsewhere analytic: `T = exp(-sigma L)`,
  `S = albedo (ambient / 2 + pi sun HG(g) T_sun) (1 - T)`. Fog volume water with the legacy fog: the fog passes already
  fogged the scene under it, no analytic medium.
- Glow: the copied glow attachment is refracted and attenuated with the scene; direct glints also feed glow, so bloom works with `r_bloomSceneIntensity 0`.
- Glints: GGX (`D_GGX`, `V_SmithJointApprox`, exact Fresnel at VH) of the sun (cascaded shadow map lookup, needs
  `r_sunlightMode` and a sun view) and of the view's dynamic lights (saber / bolt / explosion lights, lightall's
  attenuation and spot cone).
- Linear light: a legacy (display encoded) HDR scene, cubemaps, SSR samples and light colors are decoded, the result
  encoded again (`tr.linearLight`).

## Snell's window and total internal reflection (r_waterSnell)

The prompt-1 view from inside already refracted with `eta = ior` and its exact Fresnel was 1 beyond the critical angle,
but the reflected light was a constant liquid color, the window sample a fixed 256-unit guess and the Fresnel of one
wave normal per pixel. `r_waterSnell 1` replaces that inside view; above the water nothing changes (air -> water,
`eta = 1 / ior`, no TIR possible: the same code, identical output in the tests).

Physics (dielectric interface, unpolarized): `eta = n_incident / n_transmitted`: from air `1 / ior`, from the liquid
`ior` (`r_waterSurfaceIOR`, no separate IOR cvar). Snell: `sin(theta_t) = eta sin(theta_i)`; from the liquid
`sin(theta_t) >= 1` beyond the critical angle `theta_c = asin(1 / ior)` (48.6 degrees at 1.333): total internal
reflection, the exact Fresnel is 1 and nothing is transmitted. Inside the critical angle the whole sky (180 degrees) is
compressed into a cone of 2 theta_c (~97 degrees), Snell's window; Sea of Thieves shows the scene above the same way
from below (SIGGRAPH 2018, "The Technical Art of Sea of Thieves"). Here the window is not a mask: it is where
`refract()` exists, and its edge is the Fresnel rising continuously to 1.

- Side: per fragment, `dot(V, Ng) < 0` against the outward normal of the liquid brush face (the deformed surface,
  not the static brush planes the camera contents use). The camera contents (`R_LiquidPointClass`) are only shown by
  debug view 1, where they disagree (camera within a deformed wave).
- Fresnel from inside: the exact `FresnelDielectric(cos, ior)` averaged over the centre wave normal and +- one deviation
  of each unresolved slope (the LEAN variance of the mips): the critical angle widens with the waves of a pixel instead
  of aliasing; TIR in all five: `W = 1` exactly. From inside the EnvBRDF LUT is not used (it is an air-side, F0 fit
  without TIR).
- Refraction water -> air: `refract(-V, N, ior)` (the most transmissive of the five normals where the centre one is in
  TIR); bounded first guess from the scene straight behind, two fixed point steps on the height of the sample above the
  surface plane, depth rejection of samples in the liquid in front of the surface, sky: 256 units. Weight `1 - W`.
- Reflection under the surface, weight `W` (1 in TIR, no `r_waterSurfaceReflection` boost): the scene in the liquid.
  SSR (the same `ssr_common` march, started into the liquid) -> the parallax cubemap of the surface only when its probe
  is in the liquid (a probe above captured the air side), path = its parallax radius -> the liquid itself. Each through
  the analytic medium along the reflected path of length d: `L = L_hit T + S`, `T = exp(-sigma d)`,
  `S = albedo ambient_up / 2 (1 - T) + albedo pi sun HG(g) / (1 + k) (1 - exp(-sigma (1 + k) d))`, k = depth gained
  per unit / cos of the refracted sun (the exact integral for a sun attenuated down to each point). Without SSR and
  cubemap d is endless: the reflection is the liquid's in-scattering (dark in clear water, its color in murky water).
- The face of a liquid brush against a wall / floor seen from inside (scene < 4 units behind) stays transparent.
- Composite unchanged: `(1 - W) transmitted + W reflection`; the camera -> surface segment is fogged by the froxel
  composite (r_volumetricWater) as before.
- A permutation (`USE_WATER_SNELL`, `WATERDEF_USE_SNELL`), not a uniform branch: a uniform branch moved Intel's code
  generation by 1 ulp with the feature off; with the permutation `r_waterSnell 0` is the prompt-1 program, bit
  identical on Intel and NVIDIA (captured before / after), and the cvar still toggles live (8 water programs instead
  of 4 at load).

Not done: the n^2 radiance law across the interface (rend2 does not scale radiance on refraction either way); fog of
the air seen through the window; the sun's transmitted glint seen from below (the sky in the scene copy is refracted,
no BTDF lobe); the legacy `refraction.glsl` (`etaG = 1 / 1.30`, refractive shaders, both sides) is untouched: water in a
water view never goes through it (`RB_WaterSurfaceDistortion`).

Debug `r_waterSnellDebug` (cheat, needs `r_waterSnell 1`, wins over `r_waterSurfaceDebug`): 1 crossing (blue air ->
water `eta = 1 / ior`, orange water -> air `eta = ior`, magenta stripes: camera contents disagree) - 2 TIR share of
the wave normals (red) / transmits (green), yellow line at the critical angle, dark blue from air - 3 exact Fresnel of
the centre normal - 4 refracted direction (world `* 0.5 + 0.5`, black in TIR) - 5 reflection (red) / transmission
(green) weights - 6 reflection source (red SSR, green cubemap, blue liquid / fallback).

## Cvars

| cvar | default | |
|---|---|---|
| `r_waterSurface` | 0 | master, archive + latch (vid_restart) |
| `r_waterSurfaceIOR` | 1.333 | Fresnel and refraction |
| `r_waterSurfaceRoughness` | 0.06 | base perceptual roughness; unresolved waves add to it |
| `r_waterSurfaceNormal` | 1.0 | wave normal strength |
| `r_waterSurfaceRefraction` | 1.0 | screen-space refraction offset scale |
| `r_waterSurfaceDepthReject` | 1 | foreground depth rejection of the refraction |
| `r_waterSurfaceReflection` | 1.0 | reflection scale |
| `r_waterSurfaceSSR` | 1.0 | Water SSR weight over the cubemap; independent of opaque `r_ssr` |
| `r_waterSurfaceAbsorption` | 1.0 | shared extinction scale of the surface and volume |
| `r_waterSurfaceDepthScale` | 1.0 | path length scale |
| `r_waterSurfaceExperimental` | 0 | name based classification |
| `r_waterSurfaceDebug` | 0 | cheat, views below |
| `r_waterSurfaceSplit` | 0.5 | split position of debug view 9 |
| `r_waterSnell` | 0 | Snell's window / TIR from inside (archive, live) |
| `r_waterSnellDebug` | 0 | cheat, views 1-6 of the Snell section |

Commands: `r_waterInfo [surfaces]`, `r_waterOverride`.

## Debug views (r_waterSurfaceDebug, written into the HDR scene)

1 classified surfaces (blue water, green slime, violet fog volume medium, yellow override, magenta experimental,
darker from inside) - 2 normal - 3 Fresnel / reflection weight - 4 path length (r 512, g 128, b 32 units) - 5 raw
refracted color - 6 SSR hit (green, confidence) / miss (red) - 7 reflection source (red SSR, green cubemap, blue fallback)
- 8 transmittance - 9 split: legacy stages left, water program right - 10 refraction rejection (yellow shrunk, red
none) - 11 roughness.

## Asset audit (installed base, 2026-10-04)

`python tools/rend2/water_audit.py` (read only). 12 pk3, 3412 shader definitions (the water shaders are the
`assets8_pbr1.pk3` `.mtr` replacements of `common`, `hiddenevil`, ...), 59 BSPs.

| map | shader | decision |
|---|---|---|
| t2_rancor | textures/common/water_1 | water (indoor pool) |
| t2_trip | textures/common/water_quicktrip | water |
| t2_port, taspir2 | textures/bespin/water2 | water, fog-medium (water + fog brush) |
| t3_bounty | textures/common/water2_still | water |
| t3_hevil, yavin1, yavin1b | textures/h_evil/lakewater | water (outdoor lake, patches) |
| t3_hevil, yavin1, yavin1b | textures/h_evil/wfall | legacy: waterfall (0% up) |
| yavin1, yavin1b, yavin2 | textures/common/water_yavin2 | water (up share 95% / 55%) |
| vjun1 | textures/common/water2_water1_vjun1 | water, slime optics (water + slime contents) |
| vjun2 | textures/common/water_1 | water (brush model 72) |
| kor1, kor2, taspir1/2, mp/duel5, mp/ffa5, mp/siege_korriban | lava shaders | legacy: lava |

- Every stock pool / lake / river is covered by the BSP semantics: **no overrides, no overlay pk3**.
- No stock shader uses `refractive`. No MP map has water.
- Without water semantics: `textures/factory/coolant_test` (t1_fatal, a coolant liquid, `nonopaque` only) and
  `textures/factory/ggoo1` (t2_port goo, opaque blend); both stay legacy (`r_waterOverride textures/factory/coolant_test on`
  if wanted).
- Defined, unused by the stock maps: `textures/common/water2*`, `water_3`, `textures/yavin/water*` (fog / nofog /
  2sided variants), `textures/imp_mine/slime`, `textures/rmg/water1`, ...; waterfall shaders without water contents
  (`water2_waterfall*`, `water_waterfall_2/3`, `yavin/waterfall`).

## Validation

Done (no game launch):
- MSVC builds of both renderers (`rd-rend2_x86_64`, `rdsp-rend2_x86_64`), no warnings in the new code.
- `tools/rend2/test_watersurface_gl.py` on Intel UHD and RTX 2060 (`SHIM_MCCOMPAT=0x800000001`): 72 permutations
  compile and link; path length vs Snell reference (shallow 16 u, deep 400 u: < 0.1%; grazing < 7 degrees reported,
  p95 26%); Fresnel weight (< 0.001); sky; rejection (0 px with, 156 px without); shallow T 0.975 vs deep 0.54; waves
  raise roughness with distance; SSR (linear and Hi-Z) finds the reflected object; inside view TIR / Snell's window.
- Classification coverage: `water_audit.py` (the same rule).
- r_waterSnell (2026-10-04), same tests, 144 permutations: TIR exactly beyond the critical angle at IOR 1.333 and 2.0
  (0 misclassified pixels outside a 0.002 cos band), weight = exact water -> air Fresnel (< 3e-4), rising continuously
  to 1; the roof refracted into the window; TIR without SSR / cubemap = the analytic endless-path in-scattering
  (< 1e-4); SSR from below reflects the pillar in the liquid, attenuated; above the water identical to the prompt-1
  program; camera at z = +-1, +-0.01 (flat / waves) finite with consistent sides; steep ripples (normal 4) break the
  window up with partial TIR shares; IOR 1: no TIR, no offset; debug views. `r_waterSnell 0` images bit identical to
  captures of the prompt-1 shader (above, grazing, below; flat / waves; SSR; debug views) on both GPUs.

In game (user, r_waterSnell): t2_rancor / t3_hevil from below (window, TIR of the pool walls / floor), shallow grazing
view from below, diving through the surface (debug 1), ripples (`r_waterSurfaceNormal 2-4`), `r_waterSurfaceSSR 0`,
`r_cubeMapping 0`, `r_waterSnellDebug 1-6`, toggling `r_waterSnell` 0 / 1 against the prompt-1 view.

In game (user): t2_rancor (indoor pool, dark), t3_hevil / yavin1 (outdoor lake + waterfall, sun), t2_trip, vjun1
(slime), t2_port (fog water); shallow edge, deep part, grazing view, camera above, camera close / crossing the surface,
saber next to water (light glint; the blade is blended and not reflected), SSR object leaving the screen, `r_waterSurfaceSSR 0`,
`r_volumetricWater` 0 / 1, generic refractive effects (force push) unchanged, `r_waterSurfaceDebug 9`.

## GPU timings (test_watersurface_gl.py --bench, 1920x1080, water over the whole view, waves)

| | RTX 2060 | Intel UHD |
|---|---|---|
| scene copy (RGBA16F + D24S8 blit) | 0.19 ms | 2.9 ms |
| water pass, no SSR | 0.68 ms | 14 ms |
| water pass, SSR linear 40 steps | 3.7 ms | 56 ms |
| water pass, SSR Hi-Z 24 / 40 steps | 2.7 / 2.6 ms | 30 ms |

From below (camera under the surface looking up, the surface over the whole view, waves):

| | RTX 2060 | Intel UHD |
|---|---|---|
| prompt-1 inside view | 0.31 ms | 6.3 ms |
| r_waterSnell, no SSR | 0.47 ms | 10.3 ms |
| r_waterSnell, SSR Hi-Z 24 steps | 1.8 ms | 25 ms |

In game the pass costs in proportion to the water on screen; the copy runs once per view with water and is
scissored to the projected union of visible world-water bodies plus the maximum refraction halo. Dynamic or
ambiguous bodies, and bounds crossing the eye plane, conservatively fall back to the complete view.

## Known screen-space limitations

- Single layer: one water surface per pixel (the nearest), blended effects behind the water (underwater particles,
  the lower part of a waterfall) are hidden by its depth, as in UE.
- Refraction and SSR only see the screen: off-screen / occluded content falls back (no refraction / cubemap); the
  refracted search is approximate at grazing angles; the offset is bounded.
- SSR reflects the opaque scene (the pyramid is built before decals and transparents): sabers, bolts and particles are
  not reflected, only their light glints; the cubemap is static and parallax corrected only.
- The camera crossing the surface clips it at the near plane; under water the medium in front of the surface comes
  from the froxel composite (r_volumetricWater) or cgame's tint.
- The analytic in-scattering uses a constant light estimate (ambient + sun at half depth); froxel media in the volume
  are straight-ray segments, not refracted.

## Caches and verification

Geometry, model/brush links and per-shader flow-stage selection are retained for the map. Overrides update
classification and original/merged surface selection without rescanning vertices. The fixed wave spectrum is
retained on the CPU across map loads and vid_restart. View constants, medium optics, SSR settings and fallback
probe selection are computed once per view. Reflection history and copied scene textures remain view-dependent;
they cannot be cached across arbitrary camera changes. `r_waterInfo surfaces` reports actual interface decisions.

`test_watersurface_gl.py` checks all 144 shader permutations, exact depth sampling even with a changed filter,
reflection prepass/resolve, history acceptance/rejection, whole-segment fade and transmitted glow, in addition to
refraction, Fresnel and Snell tests. The color-pyramid coverage weighting is shared with opaque SSR.

The optional Windows integration test `test_watersurface_runtime.py --installation <game directory>` uses
private stock assets without modifying the installation. It checks six t2_rancor interfaces against their
actual brush planes, water-only SSR above/below, runtime overrides, and writes screenshots and a log under build/.
`--map t3_hevil` checks the nine lake interfaces instead; `--half-res 1` exercises the reduced reflection target.
Both fixtures compare the classification and normal debug images on water pixels, and check the water-to-air
debug color from below, exercising the actual queued uniform upload. The standalone GLSL harness uploads
uniforms directly and cannot catch a broken `GLSL_SetUniforms` dispatcher. Vec4 arrays must be uploaded with
their full element count: uploading only `u_Water[0]` leaves reflections, depth, wave coordinates and debug
controls zero and produces a flat colored surface despite a successful water draw.

A new Intel UHD 1080p synthetic comparison (water over the entire view, waves, Hi-Z 24 steps, cold history)
measured 38.676 ms for direct full-resolution water SSR and 33.610 ms for the reduced reflection pass plus
full-resolution bilateral resolve/shading. This excludes the shared pyramid and scene copy and is not an
in-game frame-rate estimate. The older table above describes the original implementation.

The reduced water-reflection history uses three `RGBA16F` targets: premultiplied
radiance/confidence, receiver depth/normal/roughness, and a receiver-relative hit
vector/validity. Relative hit vectors avoid the precision loss that absolute world
coordinates would have in half floats. This is 24 bytes per history pixel instead
of the former 40 bytes, before read/write traffic and double buffering.
