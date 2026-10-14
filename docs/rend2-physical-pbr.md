# Rend2 physical PBR contract

This document is the authoritative boundary contract for Rend2 lighting and
materials. It describes the opt-in `r_pbrPhysical 1` path. The default
`r_pbrPhysical 0` path deliberately retains the previous mathematics for
visual regression and stock-content compatibility.

## Coordinate and scene units

JA geometry remains in JA world units. Physical calculations use one
conversion only:

```
worldUnitsPerMeter = r_pbrWorldUnitsPerMeter       (default 33.333333)
metersPerWorldUnit = 1 / worldUnitsPerMeter        (default 0.03)
distanceMeters = distanceWorld * metersPerWorldUnit
```

The default is based on an engine/content audit rather than the Quake-inch
folk rule. SP and MP use a standing player box `(-16,-16,-24)..(16,16,40)`:
64 units tall and 32 units wide. The standing eye is 36 units above the player
origin, hence 60 units above the feet. At the default scale these are 1.92 m
tall, 0.96 m wide, and a 1.80 m eye height. The 18-unit step height becomes
0.54 m. The stock/default light-grid architectural module is 64x64x128 units,
or 1.92x1.92x3.84 m. No map source geometry ships in this repository, so that
grid/module evidence plus the independently defined SP/MP player hull and eye
height is the reproducible representative-architecture check. Existing
geometry is never rescaled.

Inverse-square lights use squared metre distance. Physical emitter area is in
square metres. The CPU supplies both conversion factors through the scene
block, so shaders do not contain scattered `0.0254`, `1/64`, or equivalent
constants. Exceptional content may override `r_pbrWorldUnitsPerMeter`.

## Scene-referred radiometry

The main HDR buffer is linear, scene-referred RGB using the renderer's linear
sRGB/Rec.709 primaries and white point. Its values are a fixed-scale proxy for
photometric luminance or radiance:

```
sceneRadiance = physicalPhotometricValue * r_pbrPhysicalToScene
physicalPhotometricValue = sceneRadiance / r_pbrPhysicalToScene
```

The default scale is `0.0001`. Thus 100,000 lux is represented by 10 scene
units and a 2,000 cd/m2 emissive white is represented by 0.2. The scale leaves
ample headroom in the existing RGBA16F targets: 65,504 scene units correspond
to approximately 655 million physical units. It is deliberately a single
global scale shared by direct lights, emissive surfaces, reconstructed/baked
lighting migration, SSGI, SSR, LTC and volumetrics. Bloom, fog integration,
IBL and post effects transport this linear value; they do not define their own
physical unit.

This fixed calibration is the boundary at which future camera pre-exposure can
be introduced. Authoring units and CPU canonicalization must not change if the
storage representation later becomes pre-exposed.

`r_pbrPhysicalToScene` is a renderer calibration setting, not an artistic
light multiplier. `r_pbrLegacyReference` is the one named migration multiplier
for legacy radiance-like sources.

## RGB photometric convention

A scalar photometric value is Y (luminous intensity, illuminance or luminance,
depending on light type). An accompanying linear RGB is chromaticity. At load
or API ingestion it is normalized to unit Rec.709 luminance:

```
Y(rgb) = dot(rgb, vec3(0.2126, 0.7152, 0.0722))
canonicalRGB = rgb / Y(rgb) * physicalY * physicalToScene
```

Negative RGB is clamped to zero. A black or near-black chromaticity produces a
black light, never NaN/Inf. Consequently `color * candela` is not ambiguous:
the canonical RGB has the authored candela value as its Y component.

Texture emissive RGB is different by design: it carries relative spatial and
chromatic variation. A linear white texel is the authored luminance reference.
Texture formats flagged sRGB are decoded before shading.

## Light semantics

All conversions occur at the CPU loading/API boundary. Shaders receive a
canonical RGB value and a range window, without unit enums.

| Source | Author-facing scalar | Canonical shader value |
| --- | --- | --- |
| Point | luminous intensity in candela; optional lumens are divided by `4*pi` | scene-scaled RGB candela |
| Spot | on-axis candela; optional lumens integrate the actual inner/outer smooth cone profile | scene-scaled RGB candela plus cone window |
| Directional/sun | illuminance in lux on a surface normal to the ray | scene-scaled RGB lux |
| Rectangle/finite cylindrical line area | luminance in cd/m2 (nits) | scene-scaled emitted RGB luminance used by LTC |

For point and spot lights, incident scene illuminance is canonical intensity
divided by squared distance in metres. The range window is exactly one through
80% of the culling range, then follows a cubic smoothstep to zero at the
boundary. Range is therefore a culling/performance bound, not an intensity
unit. A finite luminous-source radius bounds the inverse square. The physical
runtime uses a documented 1 cm default for point/spot APIs that do not carry
emitter geometry, rather than a world-unit clamp.

For a spot with cosine limits `ci=cos(inner)` and `co=cos(outer)`, the renderer
uses a full-strength core plus `smoothstep(co,ci,cos(theta))`. Its integrated
profile solid angle is `2*pi*(1 - (ci+co)/2)`, so lumen input is converted to
on-axis candela by dividing by that value. Changing either cone angle therefore
does not change requested total flux. Cookies multiply this normalized base
profile and intentionally absorb/reduce flux; they are not energy-normalized.

Physical point and spot lights enter through `GetRefPhysicalLightAPI`; this
optional API accepts candela or lumens and canonicalizes once. The historical
`AddLightToScene` ABI remains unchanged and is converted automatically in
physical mode. Directional physical authoring currently uses
`r_pbrSunIlluminance`; zero selects automatic legacy migration. Area-light JSON
prefers `"luminanceNits"`; the early `"luminance"` spelling is accepted as an
alias and old `"intensity"` remains legacy data. Legacy area brightness is
mapped through the same per-map calibration as baked lighting.

Area geometry stays in world coordinates for intersection, but physical
default-range/power estimation converts its rectangle or finite-cylinder area
to m2. LTC integrates emitted
luminance over solid angle, so its inner evaluation does not need a length-unit
conversion.

Saber lines use their submitted blade radius as finite cylinder geometry.
`r_ltcSaberLuminance` is their single physical-mode artistic calibration in
cd/m2 (default 40,000 nits); the historical radiance multiplier remains only
for strict legacy mode. Their froxel approximation is canonical candela formed
from luminance times the finite cylinder's side-on projected area; it no longer
reuses radiance as though it were a point-light intensity.

### Legacy dynamic-light migration

Legacy radius encoded both brightness and reach. Physical mode converts it to
a canonical point intensity that matches the old attenuation at half the
legacy radius, then uses that radius only as the smooth range window. This is
deterministic and performed before either legacy UBO or Forward+ transport.
Both transports therefore shade the same canonical light. FX sabers, blasters,
projectiles and explosions continue to use the old API and inherit this
conversion without asset edits.

## Material contract

At the shading boundary a standard material contains:

- linear base color;
- metallic in `[0,1]`;
- dielectric F0/specular;
- perceptual roughness in `[0,1]`;
- microfacet alpha;
- ambient occlusion in `[0,1]`;
- linear scene-referred emission.

Stored and user-facing modern roughness is perceptual roughness. GGX alpha is:

```
alpha = max(perceptualRoughness * perceptualRoughness, 0.0025)
```

Code must use `PerceptualRoughnessToAlpha`; callers must not silently choose
whether to square. The standard dielectric F0 is 0.04. The existing specular
control retains its established mapping `F0 = 0.08 * specular`, so the default
`specular = 0.5` is exactly 0.04. Metallic materials take F0 from base color.

`pbrMaterialVersion 2` declares modern perceptual roughness in an explicit
shader stage. Explicit or conventionally discovered ORM/RMO maps infer version
2. Spec-gloss, scalar and generated AutoPBR inputs infer version 1 and are
migrated deterministically at the boundary: their former alpha-like roughness
is square-rooted before the common perceptual-to-alpha conversion. This avoids
an appearance jump for stock assets without changing modern packed maps.
`r_pbrLegacyRoughness 0|1` disables/enables only that input migration for A/B
comparison; all downstream BRDF code remains on the canonical convention.

`emissiveLuminance <cd/m2>` opts a glow/emissive stage into physical authoring.
A white texel has that luminance and RGB provides relative variation. Old glow
stages are multiplied only by the named legacy-reference calibration in
physical mode.

## Baked and reconstructed lighting

JA lightmaps, vertex colors and light grids are not lux and must never be
described as recovered physical measurements. Physical mode treats them as
legacy relative lighting and maps them into the scene representation.

At map load, the renderer takes deterministic non-zero light-grid luminance
statistics. It records the lower median and 95th percentile and derives one
clamped calibration from the 95th percentile. HDR grids are sampled before
quantization; LDR grids use their ambient-plus-direct decoded values. The
calibration is stored in `world_t` and reused for the life of the map—never
computed per frame. The source data itself is its cache key, so no generated
PK3 or manual map edit is needed. A future on-disk cache can version and hash
this same pure calculation without altering the contract.

Lightmaps/light-grid probes and reconstructed static lights receive that same
factor at their load/transport boundary. Reconstructed point sources also use
the legacy-radius reference conversion before froxel injection. Raw legacy
values remain relative; only the calibrated result occupies the common scene
scale.

## Exposure and output

The complete photographic convention, camera modes, calibration constants,
motion-blur coupling, bloom placement, and validation procedure are specified
in [rend2-physical-camera.md](rend2-physical-camera.md).

Exposure maps scene-referred values to display-referred values. Automatic
exposure measures linear HDR luminance and adapts its multiplier toward the
configured middle-gray/target; fixed exposure applies the corresponding EV
gain. The output transform then tone-maps and encodes SDR or HDR output. The
fixed physical-to-scene factor is part of the camera calibration and its
inverse is available for meters/debugging; it is not baked into individual
post effects.

For a 200-nit SDR monitor, output code value 1.0 means the monitor's calibrated
200-nit white. It does not mean the HDR buffer contained 200. A 2,000-nit
emissive sign enters the HDR buffer at scene Y 0.2 and exposure/tone mapping
decides its final code value.

## Worked values

Using the defaults:

- 1,000 lm omni point light = `1000/(4*pi) = 79.5775 cd`, canonical scene Y
  `0.00795775`.
- 1,000 cd point light = canonical scene Y `0.1`; at 2 m its unwindowed
  incident scene Y is `0.025` (250 lux before the fixed scale).
- 100,000 lux sun = canonical direct scene Y `10`.
- 2,000-nit emissive white = canonical scene Y `0.2`.
- Standard dielectric F0 = `0.04`.
- Perceptual roughness `0.5` = GGX alpha `0.25`.
- 200-nit display white is a display calibration target after exposure and the
  output transform, not an input light value.

## Pipeline ownership

| System | Contract role |
| --- | --- |
| AutoPBR/authored ORM-RMO | Produces the standard material tuple; legacy roughness is migrated once |
| GGX/Smith/Schlick/Burley | Consumes perceptual roughness through the named alpha helper and canonical RGB lighting |
| Cubemap prefilter/IBL | Stores and samples linear scene radiance; uses the same roughness-to-alpha rule for GGX PDF |
| Legacy dlights/FX | Canonicalized on scene submission; unchanged in strict legacy mode |
| Forward+ | Transport/culling only; it does not reinterpret light units |
| LTC area lights | Consumes canonical emitted luminance and metre-based physical area |
| SSR and SSGI | Transport/estimate existing linear scene radiance; no independent brightness unit |
| Volumetric fog/reconstruction | Consumes the same canonical point/sun values and map calibration |
| HDR emissive/bloom | Emission enters scene units; bloom is a post-process threshold, not a radiometric conversion |
| Motion blur | Moves samples only and does not alter the lighting scale |
| Output transform/levels | Owns exposure, tone mapping and display encoding, never light authoring semantics |

Skin, cloth, wetness and other BRDF variants remain lobes within this material
and scene-light contract; they do not introduce a second light scale.

Stock FX, including sabers, bolts, impacts, explosions, projectiles, force
powers, muzzle flashes and environmental effects, may continue to submit the
existing color-plus-radius dlight API. The renderer tags that path as legacy
and performs the single compatibility conversion at scene submission. The
`FxPhysicalization` classifiers continue to describe emission and volumetric
shape; they do not apply a second photometric scale. This preserves existing
effect definitions while allowing new renderer clients to use the explicit
physical point and spot API.

## Compatibility, diagnostics and validation

- `r_pbrPhysical 0` is the strict old path and default.
- `r_pbrPhysical 1` enables canonical ingestion, metre distances, common map
  calibration and modern material semantics.
- `r_pbrInfo` prints mode, spatial scale, scene calibration, map statistics and
  the first submitted light's canonical intensity.
- `r_pbrInfo light <index>` inspects source type, legacy/physical provenance,
  world/metre range and viewer distance, canonical value, cd/lm or nits, finite
  source size, and estimated illuminance at the viewer.
- `r_pbrLight point cd|lm ...`, `spot cd|lm ...`, and `area nits ...` place
  known renderer test lights two metres in front of the next world view;
  `r_pbrLight list|clear` manages them.
- `r_pbrInfo 0.5` additionally prints perceptual roughness and computed alpha.
- `r_autoPBRDebug 3` visualizes material roughness; `r_forwardPlusDebug 9`
  visualizes light selection.

The dependency-free CPU contract tests cover conversion round trips, world
distance, luminance/chromaticity normalization, degenerate inputs,
roughness-to-alpha, deterministic map calibration and equality of canonical
light evaluation independent of transport. Strict legacy shaders compile
without the physical defines, preserving their prior formulas for regression.
