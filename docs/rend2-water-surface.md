# Modern water surface (r_waterSurface)

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
            -> WATER SLOT: copy renderFbo color + depth (MSAA resolved) -> water draws (replace, write depth)
            -> atmosphere -> clouds -> froxel fog composite   (fog camera -> water surface)
            -> SS_UNDERWATER, blended layers                  (depth tested against the water)
post: bloom (sees the glints), tone map, refraction pass of the refractive shaders (unchanged)
```

- The water program is drawn once per surface (`RB_IterateStagesWater`), its items tagged
  (`RenderState::waterSurface`); `RB_SubmitRenderPass` takes them out of the sorted order and draws them in the slot.
  A legacy fog pass of a water surface follows it in the slot (froxel views: the composite fogs it instead).
- A refractive shader classified as water (on a liquid brush) is drawn in the main pass by the water program,
  not by the refraction pass (`RB_WaterSurfaceDistortion`).
- `r_waterSurface 0` (default, latched): no targets, no programs, no extra pass, `RB_SubmitRenderPass` and the
  stage iterator take their old branches; only the per map classification (CPU, < 1 ms) runs, for `r_waterInfo`.

## Classification

Per map (`R_WaterClassifySurfaces`, after the surfaces are loaded):

1. BSP semantics: a drawn surface whose BSP shader has `CONTENTS_WATER` (`surfaceparm water`) is a candidate;
   lava contents never are; `CONTENTS_SLIME` gives slime optics.
2. The liquid brush it lies on: every vertex inside the brush and on one of its planes (all liquid brushes of all
   models, fog volumes included). A fog volume brush marks the shader `fog-medium` (its fog is the medium).
3. A shader is drawn as water when at least half of its area faces up, its downward faces left out (a liquid
   brush draws its bottom and sides too): waterfalls and streams face up nowhere and keep their stages.
4. An existing `refractive` shader lying on a liquid brush is water too; other refractive shaders stay generic.
   The optics then follow an env.json `"Liquids"` profile (water / slime) matching the shader name, the same rule
   that gives the liquid brushes under it their froxel medium (docs/rend2-volumetric-fog.md, "Class and medium").
   The froxel segment ratio is used when the volume holds a brush of that medium (vjun1: slime).
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
  ambient (`sunAmbCol`). No separate tracer. Without `r_ssr` the water uses cubemap / fallback only.
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
- Glints: GGX (`D_GGX`, `V_SmithJointApprox`, exact Fresnel at VH) of the sun (cascaded shadow map lookup, needs
  `r_sunlightMode` and a sun view) and of the view's dynamic lights (saber / bolt / explosion lights, lightall's
  attenuation and spot cone).
- Linear light: a legacy (display encoded) HDR scene, cubemaps, SSR samples and light colors are decoded, the result
  encoded again (`tr.linearLight`).

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
| `r_waterSurfaceSSR` | 1.0 | SSR weight over the cubemap (needs `r_ssr`) |
| `r_waterSurfaceAbsorption` | 1.0 | extinction scale of the liquid optics |
| `r_waterSurfaceDepthScale` | 1.0 | path length scale |
| `r_waterSurfaceExperimental` | 0 | name based classification |
| `r_waterSurfaceDebug` | 0 | cheat, views below |
| `r_waterSurfaceSplit` | 0.5 | split position of debug view 9 |

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

In game (user): t2_rancor (indoor pool, dark), t3_hevil / yavin1 (outdoor lake + waterfall, sun), t2_trip, vjun1
(slime), t2_port (fog water); shallow edge, deep part, grazing view, camera above, camera close / crossing the surface,
saber next to water (light glint; the blade is blended and not reflected), SSR object leaving the screen, `r_ssr 0`,
`r_volumetricWater` 0 / 1, generic refractive effects (force push) unchanged, `r_waterSurfaceDebug 9`.

## GPU timings (test_watersurface_gl.py --bench, 1920x1080, water over the whole view, waves)

| | RTX 2060 | Intel UHD |
|---|---|---|
| scene copy (RGBA16F + D24S8 blit) | 0.19 ms | 2.9 ms |
| water pass, no SSR | 0.68 ms | 14 ms |
| water pass, SSR linear 40 steps | 3.7 ms | 56 ms |
| water pass, SSR Hi-Z 24 / 40 steps | 2.7 / 2.6 ms | 30 ms |

In game the pass costs in proportion to the water on screen; the copy runs once per view with water.

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
- Glow attachment: water pixels write 0 (glowing things under water lose their legacy glow; modern bloom reads the scene).
