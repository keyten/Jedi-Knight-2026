# Surface-driven underwater caustics

`r_waterCausticsMode 2` makes direct-light caustics consume the same resolved
water-body surface state as the modern visible-water pass.  Mode 1 is the
previous procedural caustic unchanged, and mode 0 disables caustic modulation.
The default remains mode 1, so existing archived configurations and legacy
Rend2 output do not silently change.

## Actual integration

The source of truth is `EvaluateWaterSurfaceCommon` in
`glsl/water_surface_common.glsl`.  The visible water vertex/fragment stages and
the liquid lighting library both call it with the resolved body profile,
global wave controls, flow, time and eight precomputed spectrum terms.  There
is no second caustic wave clock or spectrum.

The body-local interactive height/velocity fields are packed into one atlas.
Both consumers use the same world-to-body mapping, atlas rectangle, texel
sizes, height and mask.  A splash therefore changes surface displacement and
normal first; the caustic changes because it evaluates that modified surface.
The atlas is allocated only for the already-budgeted interaction bodies, not
once per BSP water brush.

Micro-normal LEAN detail is deliberately excluded.  It is below the useful
receiver footprint in most Jedi Academy water and would alias.  Macro/medium
analytic waves and the interaction field are retained.  The derivative step
is the quality spacing enlarged by receiver footprint and `sqrt(depth) *
r_waterCausticsFilter`, so deeper receivers cannot resolve tiny waves.

For a receiver, existing liquid brush clipping finds the top surface and the
vertical receiver depth.  The shader:

1. evaluates the shared surface slope;
2. refracts the incoming directional-light ray using `r_waterSurfaceIOR`;
3. back-projects once to estimate its source point;
4. evaluates the neighbouring X/Y refracted footprints;
5. uses the absolute 2x2 footprint determinant as the ray-density Jacobian;
6. clamps focusing to 6x, applies focus/max-depth weighting, then multiplies
   the existing RGB Beer-Lambert transmittance.

This is world/body anchored and camera independent.  It runs inside the
existing `LiquidSunTransmittance` consumer, so opaque BSP, terrain-like BSP
faces and models using the direct-sun liquid path get one modulation.  The
same function supplies the volumetric liquid sun term; there is no separate
fullscreen overlay and no second application to the same direct-light term.
Existing CSM/geometric sun shadowing remains outside this function, so a
blocked sun does not reveal caustics.

Point and spot lights use the same function with a finite source distance.
Their direction varies at every neighbouring surface sample, so the Jacobian
is a point-source footprint rather than a directional-light approximation.
Only the first `r_waterCausticsLocalMaxLights` candidates in the already
importance-ordered Forward+/legacy list are evaluated. Area/line lights
(including the LTC saber emitter) are intentionally excluded: reducing an
extended emitter to a point produces the wrong sharp caustic.

For baked light, the caustic multiplies only the directed lobe already
separated by a deluxemap or an entity L1/dominant-light vector. In the
volumetric path the per-channel directional fraction is `|M| / B`; the
isotropic remainder of the static grid is unchanged. This avoids adding a
recovered lamp on top of its lightmap. Old BSP lightmaps without direction may
opt into the original procedural filter, but cannot claim a surface-driven
baked result.

Ambient light, diffuse IBL and emissive material output are not modulated.
They have no single incident ray bundle for a water interface to focus.
Emissive lamps still contribute when represented by a dynamic light, baked
directional lobe or volumetric reconstruction; the glowing texel itself is
not a light receiver.

## Technique decision

The selected method is the refracted-ray footprint Jacobian.  A dependency-free
CPU prototype in `tools/rend2/test_water_caustics.py` compares it with a
normal-divergence/curvature proxy on the same spectrum and injected ripple.
The curvature proxy was about 3.8x cheaper in the reference run, but correlation
was only 0.534: it loses off-diagonal shear and the refracted displacement that
makes the projected pattern visibly follow the surface.  It remains useful as
a possible low-end mode, but is not the baseline.

A per-body caustic render target was also considered.  It resembles the
traditional CryEngine water-volume caustic grid and would amortise work across
many receivers, but it adds body selection, atlas rendering, update scheduling
and filtering passes.  Jedi Academy usually has small/occluded water bodies,
and the renderer already has exact brush clipping at the direct-light
receiver, so receiver-side evaluation has lower integration and memory cost.
It also works on the OpenGL 3.2 baseline; no compute shader is required.

References: CryEngine's water-volume caustic grid uses water normals and
supports dynamic ripple influence; GPU Gems describes refracting surface
samples and projecting the resulting ray concentration.  The implementation
uses those physical ideas, not their data structures or assets.

## Controls

| CVar | default | meaning |
| --- | ---: | --- |
| `r_waterCausticsMode` | 1 | 0 off, 1 original procedural path, 2 actual surface-driven path |
| `r_volumetricWaterCaustics` | 0.35 | existing caustic strength, preserved for both paths |
| `r_waterCausticsQuality` | 1 | base Jacobian spacing: 0 = 32, 1 = 16, 2 = 8 world units |
| `r_waterCausticsMaxDepth` | 2048 | deepest receiver with surface-driven contrast |
| `r_waterCausticsSlope` | 1 | macro/interaction slope multiplier |
| `r_waterCausticsFilter` | 1 | receiver-depth footprint broadening |
| `r_waterCausticsDebug` | 0 | diagnostic view listed below; cheat-protected |
| `r_waterCausticsLightMask` | 1 | latched source bits: 1 sun, 2 dynamic point/spot, 4 directed baked; combine bits to enable sources independently |
| `r_waterCausticsLocalMaxLights` | 1 | maximum point/spot caustic evaluations per surface receiver, 0..4 |
| `r_waterCausticsBakedMode` | 0 | 0 off, 1 surface-driven when a direction exists, 2 original procedural fallback (also works on directionless lightmaps) |
| `r_waterCausticsBakedStrength` | 0.35 | blend of liquid attenuation/caustic modulation into the directed baked lobe |

Body wave/profile/flow tuning remains in the existing water-body system.
Interaction resolution and memory remain controlled by the existing
`r_waterInteraction*` CVars.

## Debug views

| value | view |
| ---: | --- |
| 0 | normal lighting result |
| 1 | sampled surface slope used by caustics |
| 2 | refracted direction of the selected direct-light source |
| 3 | raw Jacobian focusing field (0..6 mapped to 0..1) |
| 4 | strength/focus/depth-weighted result |
| 5 | projected receiver coordinate grid |
| 6 | existing RGB optical attenuation only |
| 7 | interactive-field contribution/coverage |

Views are returned at the real liquid direct-light receiver integration point. This
separates surface input, refraction/focusing, projection, optical attenuation
and interaction without adding a debug-only rendering path.

Examples: sun only (the backward-compatible default) is mask `1`; dynamic only
is `2`; baked only is `4`; all supported sources is `7`. Changing the mask
needs `vid_restart`, because unused source paths are removed from shader
permutations. The local-light limit and baked mode/strength are runtime
controls.

## Validation and cost

Automated checks:

- `test_watersurface_gl.py`: 144 OpenGL 3.2 permutations plus displacement,
  shared normal, optics, SSR/Snell/TIR and interaction checks;
- `test_liquids_gl.py`: 14 liquid permutations, 12 receiver/weather programs,
  exact brush/optical checks, and a GPU comparison proving an injected
  interaction field changes the surface-driven focusing result. Its
  `--compile-caustic-lights` mode additionally compiles GL 3.2 fast-light,
  lightmap+deluxemap and entity-L1 local/baked permutations;
- `test_water_caustics.py`: Jacobian/curvature comparison, stability and
  injected-interaction sensitivity;
- `test_water_interaction.py`: deterministic propagation and isolation.

The stock BSP audit identifies the intended runtime matrix: `t2_rancor`
(small/indoor pool), `t3_hevil` and `yavin1` (large/deeper outdoor water),
`yavin2` (streams and multiple bodies), `vjun1` (murky Vjun profile), and
`t2_port`/`taspir2` (multiple liquid volumes).  This change was not captured in
an interactive game session, so those rows are map-data coverage targets, not
claims of completed visual sign-off.  The deterministic GPU comparison is the
explicit before/after evidence supplied with the change.

The installed stock-asset audit found no cubemap-probe assets feeding diffuse
IBL on these maps and no deluxemaps in the audited BSPs. It did find many glow
surfaces near water (`t2_port` 302 candidates, `t3_hevil` 200, `taspir2` 112,
`vjun1` 112, `vjun2` 42, `t2_rancor` 25, `yavin1`/`yavin1b` 6; these are broad
spatial/facing candidates, not guaranteed emitters). This supports routing
their light through dynamic lights or baked directional reconstruction rather
than caustic-modulating emissive pixels or diffuse IBL directly.

There is no caustic-map generation pass in mode 2: generation GPU time and
new caustic-map memory are both zero. Application cost is paid only where a
selected direct-light liquid function is evaluated; dynamic lights have an
independent per-receiver cap. The shared interaction
atlas uses RGBA16F (8 bytes per atlas texel); the existing interaction info
command reports the actual allocation including packing padding.  It does not
create per-brush textures.  Mode 0 and mode 1
do not execute the Jacobian path.  Use the synthetic `test_liquids_gl.py
--bench` numbers only for regression comparisons; final per-map timings should
be recorded with the engine GPU timers on the target hardware.

Measured on the available Intel UHD (OpenGL 4.3 driver 27.20.100.8280), the
128x128 synthetic receiver benchmark was 0.162 ms with sun caustics off and
1.793 ms with the full shared eight-wave spectrum plus interaction samples
(+1.631 ms); the finite point-light path was 0.165 vs 1.953 ms (+1.788 ms).
These are 16,384 direct-light receivers and a deliberately dense compute
microbenchmark, not a whole-frame or stock-map timing.  Maps with no visible
liquid build zero receivers; indoor/shadowed receivers skip unavailable
sources through the existing lighting path. The Liquids UBO is 9,920 bytes,
an 8,240-byte increase that remains below OpenGL 3.2's 16 KiB minimum uniform
block size.  The unchanged procedural 256x256 R16F mip chain remains about 171
KiB because mode 1 must stay available.

## Known limitations and future work

- Dynamic point/spot caustics affect opaque/model receivers but are not traced
  per froxel. Putting the full brush clip and Jacobian inside every
  light/froxel iteration caused unacceptable shader compile/runtime scaling on
  the GL 3.2 baseline. A future volume path should use a cached low-resolution
  light/body projection. Saber/LTC area lights need an extended-source model.
- Surface dynamic lights reuse their normal receiver shadow/cookie. There is
  no second shadow ray from the light to the water entry point, so a small
  occluder between lamp and interface can leave a caustic where the receiver's
  ordinary shadow map does not see it.
- Surface-driven baked mode needs a deluxemap or entity dominant direction.
  Stock BSPs audited for this change have no deluxemaps; their world lightmaps
  therefore need the explicitly approximate baked mode 2, while entity L1 and
  volumetric directional moments can use mode 1.
- The receiver uses the resolved horizontal water-body interface.  Overhangs,
  waterfalls and strongly non-horizontal interfaces are outside the current
  modern-water body model.
- The one-step inverse projection is intentionally inexpensive.  Very steep
  surfaces can fold; the 6x energy clamp prevents singular flashes.
- Debug output is shared by surface and volumetric consumers rather than a
  standalone caustic-texture viewer because mode 2 intentionally has no raw
  generated texture.
