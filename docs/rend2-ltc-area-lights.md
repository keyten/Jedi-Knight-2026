# rend2 LTC area lights (`r_ltcAreaLights`)

Rectangle and line (saber) area lights shaded with Linearly Transformed Cosines.
**Forward+ only**: the legacy 32-light `dlightBits` path is untouched. Off by default.

Status (2026-09-25): implemented and built (MSVC: rend2 SP/MP, both engines, cgame, jagame, vanilla
renderers). The GLSL compiles and links on the Intel and NVIDIA drivers (240 lightall permutations:
LTC off, on, and on with debug). The LUT is checked against Monte Carlo. **The game has not been
launched yet**: the in-game checks, GPU timings and screenshots below are still to do.

## Cvars and commands

| cvar | default | |
|---|---|---|
| `r_ltcAreaLights` | 0 | archive, **latch** (vid_restart). The LTC code is only compiled into lightall when this is on |
| `r_ltcDebug` | 0 | cheat, latch. 1–8 lighting views; 9 static visibility, 10 cache slot, 11 saber cube, 12 saber screen, 13 screen confidence, 14 final saber visibility |
| `r_ltcDebugLight` | -1 | map light highlighted in modes 5–7 (-1 = the nearest) |
| `r_ltcIntensityScale` | 1 | radiance multiplier for map and scene area lights other than sabers |
| `r_ltcSaberIntensityScale` | 1 | separate radiance multiplier for LTC saber lines |
| `r_ltcStaticDiffuse` | 0 | also add diffuse for `static_specular` lights (their diffuse is normally baked) |
| `r_ltcMaxLights` | 64 | map lights per scene, most important first: emitted power / distance² (dynamic lights are not counted) |
| `r_ltcAutoAreaLights` | 1 | for maps without an `.arealights.json`: 0 off, 1 confident lamp shapes, 2 also loosely fitted ones (see "Automatic conversion"). Runs at map load only while `r_ltcAreaLights` is on |
| `r_ltcSaberAreaLights` | 0 | 0: dynamic point light; 1: LTC line on surfaces and line proxy in fog; 2: LTC line on surfaces and dynamic point light with midpoint cube shadow in fog |
| `r_ltcStaticShadows` | 0 | 0 off; 1 cached four-sample world shadows; 2 also redraw Ghoul2 casters for up to four nearby static lights per frame |
| `r_ltcSaberShadows` | -1 | -1 original midpoint cube; 0 off; 1 anisotropic cube PCF; 2 midpoint cube plus temporal screen-space line visibility; 3 three-point reference (up to two sabers, others use mode 1) |

There is no quality cvar, because the polygon integral takes no samples.

Commands:
- `r_ltcReloadLights` rereads the map file without restarting the map (or redoes the automatic conversion when there is no file).
- `r_ltcList` lists the loaded lights with their IDs.
- `r_ltcNearest` shows the nearest light.
- `r_ltcExtractLights` writes candidate lights (see below).
- `r_ltcShadowStats` shows cache, view, saber, and screen trace counters.

## LTC shadows

Static rectangle shadows keep the LTC lighting integral and multiply its diffuse and specular results by separate visibility estimates. Four 2×2 Gauss points on the rectangle each own six 128² D16 depth layers. Mode 1 renders world geometry once per cache fill, at most one new light per frame. Mode 2 adds a separate Ghoul2-only layer for up to four lights; cached world depth is reused. The cache holds up to 64 lights, reduced automatically to fit `GL_MAX_ARRAY_TEXTURE_LAYERS`. It is cleared on map unload, light reload, and renderer restart. With too few fragment samplers or array layers, rectangle lighting remains unshadowed.

Saber mode 1 filters the existing midpoint shadow cube along the projected blade. Mode 2 traces one jittered blade point per half-resolution pixel against the pre-light depth pyramid, reprojects two saber channels with motion vectors, filters by receiver depth, and blends with mode 1 according to screen confidence. Portals, mirrors, screen exits, and invalid history use the midpoint cube. The depth pyramid is reused by SSR and SSGI when those passes are enabled. Mode 3 renders two additional cubes per saber at the outer Gauss nodes and combines them with the midpoint cube using diffuse and specular weights. Volumetric fog continues to use the midpoint cube.

The shadow modes are runtime CVars. Their GPU resources are allocated when `r_ltcAreaLights` is enabled at renderer startup so modes can be switched without a restart. Screen mode 2 requires at least 29 fragment samplers; static and reference maps require at least 28. When those resources are unavailable, saber modes 2 and 3 use mode 1's cube filter. Compare modes on the same recorded scene and inspect `r_ltcDebug 9` through `14` before tuning quality or cost.

Dependency messages are printed once each time the cvars change, not every frame, and only the first
missing dependency is reported:
- "LTC area lights require r_forwardPlus 1": LTC stays inactive until Forward+ is turned on, then
  works without touching the cvar again.
- "Saber area lights require r_ltcAreaLights 1".
- "LTC area lights need more than 21 texture units".

## Data model and Forward+ integration

Area lights are `dlight_t` entries with extra fields: `areaType` (`DLIGHT_POINT`/`RECT`/`LINE`,
with DISK and SPOT reserved), `areaRight`, `areaUp`, `halfWidth`, `halfHeight`, `areaFlags` and
`areaId`.
- `origin` is the center. `color` is the emitted **radiance** (not normalized).
- `radius` is the cull sphere: range + half diagonal.
- The emitting side is `cross(right, up)`.

Flags: `AREALIGHT_TWO_SIDED`, `AREALIGHT_SPECULAR_ONLY`, `AREALIGHT_DYNAMIC`,
`AREALIGHT_SELECTED` (debug highlight).

They go through an area-aware Forward+ importance sort and conservative rounded-rectangle or
capsule cluster culling
(`R_ForwardPlusLightRange`). The light buffer grows from 3 to 5 RGBA32F texels per light:

| texel | point light | area light |
|---|---|---|
| t0 | origin, radius | centre, influence range |
| t1 | color, 0 | radiance, type |
| t2 | shadow slot, 0, 0, 0 | shadow slot (-1 for unshadowed), flags, halfWidth, halfHeight |
| t3 | – | right, conservative cull radius |
| t4 | – | up |

Point lights still fetch only 3 texels. Dynamic saber lines may own a point-shadow cube at the
blade centre; static rectangles remain unshadowed. Area lights are kept out of:
- `R_GetUboDlights` (the legacy Lights block),
- flares.

**Physical evaluation vs culling.** The geometry itself does the falloff. `range` only drives a
smooth window, `(1 - (d/range)^4)^2`, measured from the closest point on the emitter, so the cull
radius is never visible. If a map light has no `range`, one is derived from where irradiance falls
below about 1% of radiance, and it is never less than the emitter size + 16.

## Shader formulas (lightall.glsl, `EvaluateAreaLight`)

Inputs:
- The receiver tangent frame is (T1 in the N–V plane, T2, N).
- The quad corners relative to the receiver are `c∓R∓U`.
- FF = form factor, i.e. the cosine-weighted solid angle / π. It is computed with Hill's edge-vector
  rational fit, and horizon clipping uses the sphere approximation (LUT 2 `.w`).

Terms:
- **Specular**: `L · FF(M⁻¹·quad) · (F0·norm + (1−F0)·fresnel·sat(50·F0.g))`. The last factor is
  the same no-specular cut as `F_Schlick`.
- **Diffuse**: `L · albedo · FF(quad)`. This is the exact Lambert form factor, and it is skipped for
  `SPECULAR_ONLY`.
- **Cloth BRDF**: there is no second LUT. The Charlie lobe is evaluated at the closest point of the
  emitter, weighted by π·FF, because the lobe is wide and a separate table would not show.
- **Burley diffuse**: area diffuse uses Lambert (a small difference).
- **Line (saber)**: a ribbon one tube diameter wide, rebuilt per pixel to face the receiver (it has
  the same projected area as the tube). This gives an elongated highlight, not a sphere-like one.
  It is always two-sided.
- **One-sided emitters**: nothing reaches the receiver from behind the emitter plane, and nothing
  reaches the back of the emitter.
- **Vertex-lit (non-per-pixel) programs** get no area lights.

## LUT: source, generation, license

`tools/ltcfit/ltcfit.cpp` is our own fitter, written from Heitz et al. 2016 ("Real-Time
Polygonal-Light Shading with LTC") and Hill & Heitz 2016. It uses no code or data from the reference
implementation, so no third-party license applies. The papers are cited in the source.
- Model: GGX with Rend2's joint Smith visibility approximation (`V_SmithJointApprox`),
  visible-normal importance sampling, and Nelder–Mead on
  (m11, m22, m13) with 2×32×32 MIS samples per texel. It runs in about 45 s on all cores.
- Output: `shared/rd-rend2/tr_ltc_data.h`, which is compiled in and uploaded as two 64×64 RGBA16F
  textures (units 20 and 21). Nothing is fitted at startup.
- **u axis** = `sqrt(alpha)`, where alpha is rend2's `roughness` (lightall uses it directly as the
  GGX alpha). **v axis** = `sqrt(1 − N·V)`. The texel centers use the usual
  `(size−1)/size, 0.5/size` scale and bias.
- Table 1: M⁻¹ normalized by `M⁻¹[1][1]`, as `(m00, m02, m20, m22)`.
- Table 2: `(norm, fresnel, 0, sphereFF/|F|)`. The `.w` channel is indexed by
  `(z·0.5+0.5, |F|)`.

Regenerate with:

```bash
g++ -O2 -std=c++17 -o ltcfit tools/ltcfit/ltcfit.cpp -lpthread && ./ltcfit shared/rd-rend2/tr_ltc_data.h
```

Self-test: build with `-DLTC_TEST_HEADER='"path/tr_ltc_data.h"'` and run `ltcfit -test`. It compares
200 random rectangles against a Monte Carlo reference:
- **diffuse**: 0.00% error,
- **specular**: 8.71% mean absolute error relative to the mean, concentrated in grazing or
  barely-overlapping cases where the values are tiny (typical for LTC fits),
- norm at alpha→0, normal incidence = 1.0000.

## Authoring: `maps/<map>.arealights.json`

A human-editable file, separate from the BSP. It can be packed in a PK3, and
`r_ltcReloadLights` reloads it live. See `docs/samples/example.arealights.json`.

Top level: `{ "lights": [ ... ] }`.

Rectangle fields:
- `type` `"rect"`
- `center`, `right`, `up`. `up` is re-orthogonalized, and the emitting side is `right × up`.
- `halfWidth`, `halfHeight`
- `color` (default 1 1 1) and `intensity` (default 1). Radiance = color × intensity.
- `range` (optional)
- `mode`:
  - `static_specular` (default): the stock lightmapped lamp gets specular only.
  - `static_full`: diffuse + specular.
  - `dynamic`: diffuse + specular, and the diffuse also feeds SSGI.
- `twoSided` (default false)
- `name`

Line fields: `type` `"line"`, `start`, `end`, `radius`, plus the same color / intensity / range /
mode fields.

## Automatic conversion of stock maps (`r_ltcAutoAreaLights`)

Stock maps ship without a light file. Their lamps are still lit by the lightmap, so without area lights
LTC does nothing on them. When a map has no `maps/<map>.arealights.json`, its lamps are converted at
map load, in memory. The map is never changed. They become `static_specular` lights, so there is no
double lighting: the lightmap has no specular, and these lights add only the highlight.

What the stock data looks like, checked offline on 10 stock SP/MP maps with the same algorithm:
- Almost no stock lamp sets `surfacelight`: only a few MP track lights do.
- A lamp is a lightmapped surface plus an additive `glow` stage. Its mask texture is lit only where
  the lamp is, often a thin strip or several spots inside a larger texture.
- Models baked into the BSP often have glow maps with tiny details (ships, bridges, rings). Their
  average brightness is 0.000–0.008.

Algorithm (`R_FindAreaLightCandidates` in `tr_arealights.cpp`):
1. Take shaders with a glow or emissive stage, or a surfacelight hint; skip sky and nodraw. Group
   coplanar, vertex-connected triangles of one shader.
2. Read back the emitting stage's texture once per image, as a mip level of at most 128×128 converted
   to linear color. Sample the group in texture space, up to 96×96 samples: a sample inside a
   triangle whose mask texel has luminance > 0.1 becomes a world point with that color (through the
   triangle barycentrics).
3. Split the lit samples into connected blobs (8-neighbour). Each blob is one light, so a texture
   with two tubes or a row of bulbs gives one rectangle per tube or bulb.
4. Fit the rectangle to the blob along its principal axis in the surface plane, with half a sample of
   margin. It emits along the face normal.
5. **Radiance** = blob power / rectangle area (sum of colors × area per sample, times the stage's
   emissive color and scale or its constant color), capped at the brightest texel. This is the
   brightness the lamp is drawn with, and it keeps its energy.
6. **Confidence** = lit area / rectangle area.
7. If the stage's texture coordinates move (tcMod, tcGen), the mask cannot be sampled: the whole
   surface becomes the rectangle, with the texture average as its radiance.

Acceptance:
- **Rejected**: blinking or animated stages (rgbGen wave, animMap, deforms); lit area < 16 units²
  (8 in mode 2); a side < 2 units; radiance < 0.02.
- **Required confidence**: ≥ 0.6 in mode 1, ≥ 0.35 in mode 2.
- Duplicate fragments are dropped.

Offline result (mode 1): lamps are found as proper shapes. Examples:
- `vjun/lights3` → 17.8×4 tube strips, confidence 0.93.
- `hoth/lights_tube` → 112×48 panels.
- `desert/s_light` → 38.6×3.2 strips.
- MP track lights, street lamps, `hoth/light_ceiling`, `impdetention/light_blue`.

Counts range from 0 (kor1, which has no lamps) to about 400 lights per map; t3_hevil has about 4000
small light strips. Only `r_ltcMaxLights` of them are used per scene, chosen by importance.

Remaining false positives are small real emitters: wall indicators, antenna lights, glowing panels.
They are genuinely emissive, and their highlights are small.

Brightness is only as right as the glow textures are. Adjust globally with `r_ltcIntensityScale`,
or write a light file for a map.

Cost: once per map load, only while `r_ltcAreaLights` is on. It is one texture readback per emitting
image plus the sampling; the per-frame cost is a sort of the map's lights by importance.

`r_ltcExtractLights` writes the same candidates to `maps/<map>.arealights.generated.json` for
review or hand tuning. `"review": false` marks the ones the automatic mode would take; the file also
has `fittedToTexels`, `litArea`, `confidence` and `animated`. It is never loaded by itself; rename it
to `<map>.arealights.json` to use it (a file always overrides the automatic mode).

The shader parser keeps `surfacelight` / `q3map_surfacelight` and `lightColor` / `q3map_lightRGB`
in `shader_t` as hints. Nothing renders from them, so legacy behavior is unchanged.

## Sabers

- cgame `CG_DoSaber` / `CG_DoSaberLight` (SP and MP) call `AddLineLightToScene(base, tip, radius/2,
  length·1.4, rgb)` when the `r_ltcSaberAreaLights` mirror is on.
- Each blade becomes its own line. Only if the renderer returns false is the old point light added,
  so the two are never both active.
- Blade radiance = saber color × 4 × `r_ltcSaberIntensityScale` (`SABER_AREA_RADIANCE`).
  In mode 2, fog uses the original saber color and the midpoint dynamic shadow cube, so changing
  LTC saber intensity only changes surface lighting.

API, chosen to stay compatible (`refexport_t` and `REF_API_VERSION` are **unchanged**, so the A/B
renderer DLLs keep loading):
- rend2 exports an optional `GetRefAreaLightAPI` symbol that returns `refAreaLightExport_t`
  (`AddAreaLightToScene`, `AddLineLightToScene`). The engines look it up (`reAreaLights`, NULL for
  other renderers).
- SP adds syscall `CG_R_ADDLINELIGHTTOSCENE` (appended). MP adds `cgameImport_t.ext.R_AddLineLightToScene`
  (appended; legacy VM cgame returns false).
- cgame only calls it when `r_ltcSaberAreaLights` is set, so an old engine with a new cgame stays safe
  unless that cvar is turned on.
- Using saber lines needs the rebuilt engine and game modules as well as the renderer.

## Interactions

- **SSGI**: only diffuse from non-spec-only lights is added to `g_ssgiDynamicDiffuse`; specular
  never is. Static spec-only lamps add nothing. The emissive lamp surface stays an SSGI source on
  its own.
- **SSR / cubemaps**: unchanged and additive. A surface shows both the direct LTC highlight and the
  reflection.
- **POM self shadow**: one ray towards the emitter center, ranked with point lights under the
  same per-pixel budget. The area score uses a conservative emitter bound; there are no per-corner rays.
- **Shadows**: dynamic saber lines use a point-shadow cube centred on the blade for LTC visibility
  and froxel fog. It is a single-point approximation of line visibility. Static rectangles remain
  unshadowed and rely on the lightmap.
- **Volumetric fog**: saber lines use a froxel-only proxy located at the closest point on the
  blade. Static rectangles are not injected.

## Validation still to do in game

For each scene below, check with `r_ltcDebug 1/2/3/6/7`, and confirm that setting
`r_ltcAreaLights 0` + vid_restart returns the legacy image:
- a rectangle above glossy, rough, metallic and plastic floors;
- the back of a lamp and a lamp seen edge-on;
- a hangar light and multiple lights;
- a static lightmapped room (no double lighting);
- a saber near a wall and over a shiny floor, two sabers, and a moving saber;
- SSGI, SSR, POM and volumetric fog, each on and off.

GPU timing: compare `r_forwardPlusBenchmark` with LTC on and off (the table is still empty). Offline
compile cost of one lightall program with `USE_LTC`: +16% on Intel, within noise on NVIDIA, and zero
when off.

## Limitations

- The saber is a ribbon, not an exact line or tube integral.
- Static rectangles have no area shadows or volumetric scattering. Saber lines use a closest-point
  fog proxy and a point-shadow cube at the blade centre.
- The specular LUT has a 9% mean error at grazing angles.
- Vertex-lit surfaces are not lit by area lights.
- Automatic lights: brightness follows the glow textures; curved lamps (patches) and lamps without a glow stage are not found; only flat faces and triangle soups are scanned.
- The `range` default is heuristic.
