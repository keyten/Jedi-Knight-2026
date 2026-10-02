# Rend2: long range atmosphere and aerial perspective (`r_atmosphere`)

The air between the camera and distant surfaces, and the sky, as a separate long range component next to the
froxel fog. The froxels (`r_volumetricFog 2`, [rend2-volumetric-fog.md](rend2-volumetric-fog.md)) keep the local
media — rooms, fog volumes, smoke, height fog — up to `r_volumetricFogFar`, plus their analytic tail. The froxel grid
is **not** stretched to the horizon for this: neither `r_volumetricFogFar` nor the slice count changes.

| component | covers | where |
|---|---|---|
| local froxel media | BSP fog volumes, height fog, local volumes, FX media | `tr_volumetric.cpp` |
| aerial perspective | air between the camera and opaque surfaces, long range outdoors | `tr_atmosphere.cpp`, `atmosphere_composite.glsl` |
| sky atmosphere | horizon haze over the skybox, or an analytic sky | same, sky-view LUT |

Default off (`r_atmosphere 0`). No asset changes: the existing sky shaders, skyboxes and map suns are used.

Files: `shared/rd-rend2/tr_atmosphere.cpp`, `glsl/atmosphere_common.glsl` (library), `atmosphere_transmittance.glsl`,
`atmosphere_multiscatter.glsl`, `atmosphere_skyview.glsl`, `atmosphere_composite.glsl`; checks in
`tools/rend2/atmosphere_check.py` (CPU) and `tools/rend2/test_atmosphere_gl.py` (GPU).

## Model

Hillaire 2020, *A Scalable and Production Ready Sky and Atmosphere Rendering Technique* (EGSR 2020), with the Earth
parameters of Bruneton's `precomputed_atmospheric_scattering` (2017):

| | value |
|---|---|
| planet / top radius | 6360 km / 6460 km |
| Rayleigh scattering | (5.802, 13.558, 33.1) · 10⁻⁶ /m, scale height 8 km |
| Mie scattering / extinction | 3.996 · 10⁻⁶ / 4.40 · 10⁻⁶ /m, scale height 1.2 km, Cornette-Shanks g = 0.8 |
| ozone absorption | (0.650, 1.881, 0.085) · 10⁻⁶ /m, tent 10–40 km, peak at 25 km |
| ground albedo (multiple scattering) | 0.3 |

Hillaire's LUTs are used for the sky. **One deviation**: the aerial perspective of the scene is not a camera
froxel volume. JA paths are short compared to the planet, a few km of air even with the distance scale, so along
the path:
- the planet is flat (the curvature drop over 11 km is about 10 m);
- the optical depth of the exponential profiles is closed form;
- the source term (sun transmittance from the transmittance LUT, multiple scattering from the MS LUT) is taken at
  the transmittance midpoint of 3 segments;
- ozone is skipped, because its density is 0 below 10 km.

Per pixel, the cost is 3 segments with 2 LUT fetches each, with no per-frame volume. Over the JA domain (up to
1.5 km of real air × an aerial scale up to 30, every camera altitude, view elevation, sun elevation and phase angle
tested), the error against a reference ray march (`atmosphere_check.py`) is:

| segments | worst in-scattering error | worst transmittance error |
|---|---|---|
| 1 | 4.2 % | 6 · 10⁻¹⁰ |
| 2 | 1.2 % | 6 · 10⁻¹⁰ |
| **3 (used)** | **0.55 %** | 6 · 10⁻¹⁰ |
| 4 | 0.31 % | 6 · 10⁻¹⁰ |

## Units

JA world units are not metres.
- The player box is 64 units tall (`DEFAULT_MINS_2 −24` .. `DEFAULT_MAXS_2 40`, about 1.8 m), and the eye is about
  50 units above the feet (about 1.6 m). That gives 0.028–0.032 m per unit.
- Q3 lore says 1 unit is 1 inch (0.0254 m).
- Movement physics (gravity 800 u/s²) would give 0.012 m per unit, but game physics is exaggerated.

**`r_atmosphereUnitScale` = 0.03 m per unit.**

Consequence: `distanceCull` 12000 units is about 360 m of air, where physical aerial perspective is close to
invisible (blue Rayleigh optical depth ≈ 0.012). `r_atmosphereAerialScale` multiplies **only the aerial path
length** — not altitudes and not the sky (an artistic distance knob, like UE's "aerial perspective view distance
scale"). The default is 1 (physical); 8–30 makes JA vistas hazy.

Altitude:
- Altitude = `r_atmosphereAltitude` (metres of the ground above sea level, default 0) + (z − ground z) ·
  `r_atmosphereUnitScale`.
- Ground z is `r_atmosphereGroundZ`; `auto` is the lowest up-facing floor of the map (`R_SetHeightFogBase`, the same
  value as the automatic height fog base).
- The sky-view LUT and its lookups keep the camera at least 10 m above the ground (Hillaire's planet radius
  offset).

## LUTs

All are fragment passes (GL 3.2, no compute), generated in `RB_AtmosphereComposite` only when their key changes:

| LUT | format | size | parameterization | rebuilt when |
|---|---|---|---|---|
| transmittance | RGBA16F 2D | 256 × 64 | Bruneton (altitude, distance to the top) — rays above the horizon | the medium changes: Rayleigh / Mie / ozone scale, Mie g, sun size |
| multiple scattering | RGBA16F 2D | 32 × 32 | Hillaire: cos sun zenith, altitude; 64 directions × 20 steps, Ψ = L₂ / (1 − f_ms) | with the transmittance LUT |
| sky-view | RGBA16F 2D | 192 × 108 | Hillaire: azimuth from the sun (sqrt), zenith non linear around the horizon; 32 quadratic steps | sun direction (~0.05° steps), camera altitude (10 m buckets) or medium |

- A static map sun builds everything once. `r_forceSun 2` animates the sun, which rebuilds the sky-view LUT
  (20k texels) every frame.
- The sky-view LUT is only built while something reads it: sky modes 1 and 2, and debug views 5 and 7.
- Memory: 64 KB + 8 KB + 162 KB (≈ 234 KB of textures).
- The images and the LUT FBOs are always created (`r_atmosphere` is not latched), as are the 4 programs.

**Horizon precision.** Both directions of the sky-view mapping work with the signed angle between the view and
the horizon, through the identities cos(zenith_h) = −√(r² − R²)/r and sin(zenith_h) = R/r. They never round-trip
through `acos(cos(zenith))`. Just below the horizon of a camera a few metres up, the path to the ground changes
by about 10⁴ km per unit of cos zenith. The GPUs' `acos` error (about 2 · 10⁻⁵ rad) made the first texel row below
the horizon 7 % too dark on both Intel and NVIDIA; it is now 0.19 %.

## Sun

- **Direction.** `refdef.sunDir`: the map sun (`sun` / `q3map_sun` / `q3map_sunExt` / `q3gl2_sun` in any shader),
  or the animated sun of `r_forceSun 2`.
- **Maps without a parsed sun** keep the atmosphere off unless `r_forceSun` is set; the reason is printed once.
  There is no fake sun, because the default direction of those maps (0.45, 0.3, 0.9) means nothing.
- **Colour (buffer units).** What a white Lambert surface lit by the sun at normal incidence shows. Sources, in
  order:
  1. the froxel fog's estimate from the sunlit light grid cells (`world->volumetricSunRadiance`), when it exists;
  2. otherwise the `r_sunlightMode 2` sun colour, `sunLight · 2^(mapOverBright − overbright − 8)`;
  3. with `r_forceSun 2`, its own colour.
- **Physical illuminance** is π × that value. Radiance per unit illuminance from the LUTs × π × colour is in buffer
  units, so the relative brightness of sky, haze and lit surfaces stays physical.
- **`r_atmosphereSunColor`** sets what the map sun means:
  - **1 (default):** the map's sun is the sun **at the ground**. The illuminance at the top of the atmosphere is
    sun / T(camera → sun), capped at 8× the luminance for a horizon sun. Authored sunset colours are not reddened
    a second time, and the haze matches the lit surfaces.
  - **0:** the map sun is the sun at the top of the atmosphere.
  - **2:** white, at the map sun's luminance.
- **`r_atmosphereSunIntensity`** scales the result. It is a source scale, not an exposure: the atmosphere never
  touches exposure, bloom or tone mapping.

### Sun disc ownership (one sun)

| | disc |
|---|---|
| `r_drawSun 1` (any sky mode) | `RB_DrawSun` (sprite, drawn after the composite, untouched) |
| sky 2 (analytic), `r_drawSun 0` | the atmosphere: angular size `r_atmosphereSunSize` (0.53°), linear limb darkening, radiance E / Ω × T(camera → sun), capped at luminance 16384 for the half-float buffer |
| sky 0 / 1 | none: the skybox may have a painted sun |

The Mie forward lobe (sun glow) is part of the in-scattering. Over the skybox (sky 0) it is scaled by
`r_atmosphereSunGlow`, so it can be toned down when the painted sun is not where the map's sun direction points.
`r_drawSunRays` and lens flares are unchanged.

## Sky (`r_atmosphereSky`)

| mode | sky pixels (depth 1) |
|---|---|
| **0 overlay (default)** | skybox kept; aerial perspective over the sky distance (the froxel fog's: `max(zFar, global fog depthForOpaque)`) × aerial scale: horizon haze, sun glow. The full sky radiance is not added (that would double the painted sky) |
| 1 blend | `mix(skybox, analytic, r_atmosphereSkyBlend)` |
| 2 analytic | sky-view LUT (+ disc). It integrates the atmosphere to space, so no aerial perspective goes on top |

- **Sky portal maps** (`skyportal` entity) always use 0: the depth-1 pixels of the main view are the portal's
  scene (distant terrain), so they get overlay aerial perspective and are never replaced.
- With **`r_fastsky`**, mode 0 hazes the clear colour.
- **Cubemaps** are captured at load time with the skybox and no atmosphere, so in mode 2 reflections still show the
  skybox.

## Rendering order

```
main view: depth prepass → AO → froxel inject / integrate
  layers <= SS_FOG (sky first, opaque, decals, see-through, banners)
  RB_AtmosphereComposite    color = color * T_atm + S_atm   (or the analytic sky), glow *= T_atm
  RB_VolumetricComposite    color = color * T_froxel + S_froxel
  transparent layers        (froxel lookup / legacy fog; no atmosphere in v1)
post: SMAA T2x / motion blur → bloom → auto exposure → tone map
```

`RB_SubmitRenderPass` splits the pass at `SS_FOG` when either composite is active (the split code is unchanged
when both are off). Along a ray:

```
L = S_froxel + T_froxel · (S_atm + T_atm · surface)
```

The atmosphere is "behind" the local media. That is exact when the local media lie in front of the air that
matters (near fog, rooms, smoke), and an approximation where both overlap (the air inside a fog volume is
attenuated by the whole volume).

The composite is two fullscreen draws, because atmospheric transmittance is coloured:
1. blend `ZERO, SRC_COLOR` writes T (or 1 − blend, or 0 for a replaced sky);
2. blend `ONE, ONE` adds S (or the analytic sky).

Destination alpha is masked. The view model (depth-hack range) gets no aerial perspective.

**Nothing is applied twice:**
- The atmosphere ignores BSP fog, height fog and local media, and the froxel tail beyond `r_volumetricFogFar`. Those
  stay the froxels' media.
- The froxel tail contains no atmosphere.
- The analytic sky gets no aerial perspective.
- **Legacy fog:** on maps with a **global fog**, the atmosphere stays off unless the froxel composite is active. The
  legacy fog cap and the per-surface fog passes are drawn before the composite, so the order would be wrong. BSP
  fog volumes with legacy fog are allowed, with that ordering approximation for distant fogged surfaces.

**HDR.** The atmosphere works in linear radiance before tone mapping. The HDR buffer is scene-linear only on maps
with HDR lightmaps or with `r_linearLighting 1`. Elsewhere it holds display-encoded values, and the composite works
in buffer space, like the froxel fog and the legacy fog. Bright sun-vicinity values (Mie lobe, disc) reach bloom
through the HDR highpass; the glow buffer is only attenuated by T.

## Views

Main world view only, the same policy as the froxel fog:
- **Mirrors and portals** (`VPT_PORTAL`) and the sky portal view keep their look. A mirror shows distant geometry
  without haze.
- **Cubemap captures** (`VPF_NOCUBEMAPS` / `VPF_NODIFFUSEIBL`, own FBO) and `RDF_NOWORLDMODEL` / hyperspace views
  are also excluded.

## Cvars

None is latched. A medium change rebuilds the LUTs through their key.

| cvar | default | |
|---|---|---|
| `r_atmosphere` | 0 | on / off |
| `r_atmosphereSky` | 0 | 0 overlay, 1 blend, 2 analytic |
| `r_atmosphereSkyBlend` | 0.5 | share of the analytic sky (mode 1) |
| `r_atmosphereUnitScale` | 0.03 | metres per world unit |
| `r_atmosphereAerialScale` | 1 | aerial path length multiplier (8–30 for visible JA haze) |
| `r_atmosphereAltitude` | 0 | ground altitude, metres above sea level |
| `r_atmosphereGroundZ` | auto | world z of the ground, auto = lowest floor |
| `r_atmosphereRayleigh` | 1 | Rayleigh density multiplier |
| `r_atmosphereMie` | 1 | Mie (haze, aerosol) density multiplier |
| `r_atmosphereMieG` | 0.8 | Mie anisotropy |
| `r_atmosphereOzone` | 1 | ozone density multiplier (sky colour) |
| `r_atmosphereSunColor` | 1 | 0 map sun at the top of the atmosphere, 1 map sun at the ground, 2 white |
| `r_atmosphereSunIntensity` | 1 | sun illuminance multiplier |
| `r_atmosphereSunSize` | 0.53 | sun angular diameter (degrees) |
| `r_atmosphereSunGlow` | 1 | Mie glow over the skybox (mode 0) |
| `r_atmosphereStart` | 0 | world units before which there is no aerial perspective |
| `r_atmosphereDebug` | 0 | cheat, debug views below |

`r_atmosphereInfo` prints:
- the state or the reason it is off, and the map sun;
- ground z and its source, sky portal and global fog;
- the LUT builds;
- for the last composite: the effective units ("1000 units = N m of air"), camera altitude, sky distance, sun
  elevation and transmittance, map and top-of-atmosphere sun, sky mode, disc owner and HDR buffer encoding.

Per map tuning: none for now. If needed, the place is an `"Atmosphere"` object in the existing per-map `env.json`
(`R_LoadEnvironmentJson`, next to `FogVolumes` / `HeightFog`), not shader edits.

## Debug views (`r_atmosphereDebug`, in the HDR scene, tone mapped)

| view | shows |
|---|---|
| 1 | Rayleigh in-scattering of the aerial path |
| 2 | Mie in-scattering |
| 3 | aerial transmittance T (rgb) |
| 4 | aerial in-scattering S |
| 5 | atmosphere-only sky: the sky-view LUT in every direction, geometry ignored (+ disc in mode 2 conditions) |
| 6 | composition: red = froxel opacity (needs `r_volumetricFog 2`), green = atmosphere opacity, blue tint on sky pixels |
| 7 | LUTs: transmittance (bottom left), multiple scattering × 50 (above it), sky-view × sun (right) |
| 8 | scaled aerial distance bands: < 1, 2, 5, 10, 20 km and beyond, darkened by T |

A debug view replaces the frame and skips the froxel composite (view 6 reads the volume itself).

## GPU cost and memory

- **Per frame:**
  - the composite: 2 fullscreen draws, each with one depth fetch, a 4×4 inverse, 3 segments × (2 exp integrals, 2
    LUT fetches) and an extra MSAA depth resolve with MSAA;
  - the sky-view LUT (192 × 108 × 32 steps) only when its key changes;
  - transmittance and multiple scattering (≈ 16k × 40 and 1k × 64 × 20 samples) only when the medium changes.
- Timers: `r_speeds 100` lists "Atmosphere composite" and "Atmosphere LUTs" (GL timestamp queries).
- **Not profiled in game yet.**
- Memory: ≈ 234 KB of LUTs, 4 FBOs (the composite attaches the existing scene colour and glow), 4 programs.

## Compatibility

- `r_atmosphere 0`: the render pass and every draw are unchanged. The LUT images, FBOs and programs exist but are
  never drawn.
- No asset changes, no shader keywords, no map changes. Existing skyboxes, cloud layers and suns keep working, and
  the feature is opt-in.

## Validation

Done (no game launch):
- MSVC build of both renderers (`rd-rend2_x86_64`, `rdsp-rend2_x86_64`).
- `python tools/rend2/atmosphere_check.py`:
  - LUT parameterization round trips (< 10⁻¹⁴);
  - sun transmittance table;
  - analytic aerial perspective vs a reference ray march (table above).
- `python tools/rend2/test_atmosphere_gl.py` (hidden GL 3.2 core context; on Optimus laptops, run once more with
  `SHIM_MCCOMPAT=0x800000001` for the NVIDIA GPU). Passed on Intel UHD (27.20.100.8280) and RTX 2060 (616.92):
  - all 6 programs compile and link: 3 LUT passes, and the composite without the froxel functions and with them
    (scalar and RGB);
  - transmittance LUT vs CPU: 2.9 · 10⁻³ absolute;
  - multiple scattering LUT finite and bounded;
  - sky-view LUT (single scattering) vs a CPU ray march: 0.42 %, 0.19 % within 0.2° of the horizon;
  - composite, both blend draws on a synthetic depth buffer (geometry at several ranges, view model, sky) vs the
    CPU: 0.12 % for overlay, overlay with sun glow 0.25, and analytic;
  - zenith bluer than the horizon.

In game (to do, user):
1. **Outdoor** (yavin1b, t1_rail, hoth2):
   - `r_atmosphere 1`, `r_atmosphereAerialScale 1 / 10 / 30`, debug 8 for the ranges, debug 3/4 for T and S;
   - horizon;
   - looking toward and away from the sun: Mie glow, debug 2;
   - `r_atmosphereInfo`.
2. **Indoor map with a sky portal or windows** (t1_inter, kor1): the overlay is forced, nothing is replaced, and
   rooms are unaffected.
3. **Foggy outdoor:** `r_volumetricFog 2`, `r_vfog uniform 3000`, debug 6 (froxel red in front, atmosphere green
   behind). Also check that a global fog map with `r_volumetricFog 0` prints the off reason.
4. **Sky modes** 0 / 1 / 2 × `r_drawSun` 0 / 1: exactly one disc.
5. **Mirrors and portals:** no haze there (by policy).
6. **Low sun / `r_forceSun 2`:** orange haze, sky-view rebuilt per frame (LUT builds in `r_atmosphereInfo`).
7. **Timings:** `r_speeds 100`.

## Limitations

- Transparent (blended) surfaces get no aerial perspective. Distant water or glass is drawn without haze. The next
  step would add the atmosphere to the froxel lookup of transparent surfaces.
- Main view only (no mirrors, portals, sky portal view or cubemaps).
- The aerial path is flat and short-range (valid up to tens of km of scaled path); the sky uses the full spherical
  model.
- With display-encoded HDR buffers, the composite is in buffer space (as all fog), not physically exact.
- The camera altitude is clamped to ≥ 10 m for the sky-view LUT.
- No ground albedo in the aerial perspective; the multiple scattering LUT includes it.
