# Surface-driven underwater caustics

`r_waterCausticsMode 2` makes direct-sun caustics consume the same resolved
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

Body wave/profile/flow tuning remains in the existing water-body system.
Interaction resolution and memory remain controlled by the existing
`r_waterInteraction*` CVars.

## Debug views

| value | view |
| ---: | --- |
| 0 | normal lighting result |
| 1 | sampled surface slope used by caustics |
| 2 | refracted sun direction |
| 3 | raw Jacobian focusing field (0..6 mapped to 0..1) |
| 4 | strength/focus/depth-weighted result |
| 5 | projected receiver coordinate grid |
| 6 | existing RGB optical attenuation only |
| 7 | interactive-field contribution/coverage |

Views are returned at the real liquid-sun receiver integration point.  This
separates surface input, refraction/focusing, projection, optical attenuation
and interaction without adding a debug-only rendering path.

## Validation and cost

Automated checks:

- `test_watersurface_gl.py`: 144 OpenGL 3.2 permutations plus displacement,
  shared normal, optics, SSR/Snell/TIR and interaction checks;
- `test_liquids_gl.py`: 14 liquid permutations, 12 receiver/weather programs,
  exact brush/optical checks, and a GPU comparison proving an injected
  interaction field changes the surface-driven focusing result;
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

There is no caustic-map generation pass in mode 2: generation GPU time and
new caustic-map memory are both zero.  Application cost is paid only where the
existing direct-sun liquid function is evaluated.  The shared interaction
atlas uses RGBA16F (8 bytes per atlas texel); the existing interaction info
command reports the actual allocation including packing padding.  It does not
create per-brush textures.  Mode 0 and mode 1
do not execute the Jacobian path.  Use the synthetic `test_liquids_gl.py
--bench` numbers only for regression comparisons; final per-map timings should
be recorded with the engine GPU timers on the target hardware.

Measured on the available Intel UHD (OpenGL 4.3 driver 27.20.100.8280), the
128x128 synthetic receiver benchmark was 0.135 ms with caustics off and 1.585
ms with the full shared eight-wave spectrum plus interaction samples: +1.450
ms for 16,384 direct-sun receivers.  This is a deliberately dense compute
microbenchmark, not a whole-frame or stock-map timing.  Maps with no visible
liquid build zero receivers; indoor/shadowed receivers skip the direct-sun
consumer through the existing lighting path.  The Liquids UBO is 9,904 bytes,
an 8,224-byte increase that remains below OpenGL 3.2's 16 KiB minimum uniform
block size.  The unchanged procedural 256x256 R16F mip chain remains about 171
KiB because mode 1 must stay available.

## Known limitations and future work

- Only the primary directional light is supported.  Point/saber/explosion
  caustics need a local-light projection domain and light-specific occlusion;
  evaluating this Jacobian per dynamic light is not free.
- The receiver uses the resolved horizontal water-body interface.  Overhangs,
  waterfalls and strongly non-horizontal interfaces are outside the current
  modern-water body model.
- The one-step inverse projection is intentionally inexpensive.  Very steep
  surfaces can fold; the 6x energy clamp prevents singular flashes.
- Debug output is shared by surface and volumetric consumers rather than a
  standalone caustic-texture viewer because mode 2 intentionally has no raw
  generated texture.
