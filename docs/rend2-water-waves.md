# Ambient water waves

## What was already present

With `r_waterSurface 1`, `R_WaterBuildWaveSlopes` constructs a deterministic
256² tile of height-field derivatives. `*waterWaves` stores `(sx, sy, sx²,
sy²)` in RGBA16F and has generated mipmaps. The water fragment shader samples
two independently drifting world-space scales and a third scale through the
first stage's animated texture coordinates. Their mean slopes bend the water
normal. The difference between mean square and squared mean at each mip adds
unresolved wave energy to roughness (LEAN-like filtering). `r_waterSurfaceNormal`
scales this detail; its value of 1 retains the original effect when ambient
waves are off. The same final normal feeds Fresnel, refraction, SSR and cubemap
reflection. Inside the water, Snell/TIR also samples the micro-slope variance.
The legacy `DeformPosition` and `DeformNormal` vertex functions still run for
authored shader deforms. No generated texture sample supplies a physical
height for a large wave.

## Model

`EvaluateWaterSurface(worldPosition, time)` in `watersurface.glsl` evaluates a
deterministic sum of directional sine waves. It returns height, vertical and
bounded horizontal displacement, slope, velocity, curvature and a depth
attenuation factor. The geometric normal is reconstructed from that slope and
the base surface frame; the shader adds the existing micro slope before final
normalization. Reflection, refraction and Snell use this combined normal.

Sine components were chosen because the current mesh is not subdivided. A
Gerstner crest offers little benefit until that mesh can show a silhouette;
the returned horizontal drift is deliberately small. The evaluator transforms
the height derivative through the horizontal displacement Jacobian so its
geometric slope remains consistent with that drift. Macro components 0–3
use the body wavelength times 1, .87, .74 and .61. Medium components 4–7 use
.28, .255, .23 and .205. Quality 0/1/2 evaluates 1+1, 2+2 or 4+4
macro+medium components. A quality normalization keeps overall height similar.
Directions have angular
spread; an authored world XY `Flow` biases them without treating a legacy
texture scroll as physical flow. Terms and phases are fixed, with no runtime
random state. The CPU resolves the body profile and uploads compact parameters;
the GPU evaluates the phases per fragment. The mathematical evaluator is
independent of screen coordinates and can be moved into a shared GLSL source
for a future vertex mesh. A CPU waterline implementation must reproduce these
same constants and phase order; no independent wave equation should be added.

Existing `waterDynamics_t` values supply amplitude, wavelength, speed,
choppiness and micro-normal strength. `waterBody_t` supplies the resolved
profile, world XY flow, mean brush depth and wave multiplier. Amplitude is
mapped to world units by a fixed factor of eight; profile speed maps to 16
world units per second at a value of one. `r_waterWaveAmplitude`,
`r_waterWaveLength` and `r_waterWaveSpeed` are global scales. Choppiness only
affects the horizontal displacement returned for future geometry; it does not
alter the current shading normal. The body ID travels in unused upper bits of
the existing surface key, so separate bodies sharing a shader remain distinct
without a shader permutation or a global profile selection.

The body-average brush depth gives stable whole-body macro attenuation for
shallow water. A body without linked brush depth stays unattenuated. Brush
bounds are not a valid distance-to-wall field for irregular lakes or several
joined brushes, so this version does not fake per-edge attenuation. Future
shoreline geometry can provide an attenuation multiplier to the same
evaluator. Screen depth remains for optical path length, never for wave shape.

Authored `deformVertexes` remains active. When its recorded wave amplitude is
over 0.5 world units, the new analytical amplitude is reduced to 20% for the
body, avoiding a large second wave system. There is no new vertex displacement
in this task. When `r_waterWaves 0`, legacy deforms and all existing detail
wave samples are unchanged. The first-stage `tcMod` remains the third detail
sample. It does not determine macro wave flow. The six stock `t2_rancor`
`water_1` pools have a recorded legacy deform amplitude of 2 and exercise
this reduction; the stock `t3_bounty` still-water bodies have no vertex wave.

## Controls

`r_waterWaves 0|1` is off by default and requires `r_waterSurface 1`.
`r_waterWaveAmplitude`, `r_waterWaveLength`, `r_waterWaveSpeed`,
`r_waterWaveChoppiness`, `r_waterWaveMicro` default to 1.
`r_waterWaveQuality` defaults to 1 (four terms); 0 uses two and 2 uses eight.
`r_waterWaveShallow` defaults to 1. `r_waterSurfaceNormal` remains the existing
detail-normal control. `r_waterWaveTime -1` runs normally; a nonnegative time
freezes analytical waves and both world-space detail drifts for A/B captures;
reset it to -1 to resume. The third detail layer still follows the authored
stage `tcMod` clock.

`r_waterWaveDebug` modes: 1 height, 2 analytical geometric normal, 3 original
micro normal after profile scaling, 4 combined normal, 5 body/profile color, 6 depth attenuation,
7 displacement magnitude, 8 old/new half-screen comparison, 9 body wave
multiplier, 10 recorded legacy deform amplitude. Mode 8 preserves the original
micro strength and unresolved variance on its left side. Existing
`r_waterSurfaceDebug` and `r_waterSnellDebug` remain available.

## Scope and verification

The new height is mathematical state for future subdivided render geometry,
interactive disturbance composition, caustic input and camera-waterline
queries. Current optical path length still uses the undisplaced mesh plane;
large-wave parallax and silhouette are deferred to the geometry task. The
existing wave texture and mip roughness are unchanged. Profiles are uniform
data; no extra shader permutations, targets, textures or persistent wave
buffers were added. Per-draw data grows by seven vec4s (112 bytes).

The stock audit contains six separate `t2_rancor` pool bodies using one shader,
two `t3_bounty` still-pool bodies, a large `t3_hevil` lake selected by the
provided overlay, `yavin1` generic water and `vjun1` slime. The latter has no
linked brush depth, so it has no depth damping. The map overlay must be
installed for the `t2_rancor` and `t3_hevil` body-specific authoring examples.

The existing SP/MP rend2 targets build with the configured toolchain. The
hidden GL 3.2 test compiled and linked all 144 existing permutations on Intel
UHD Graphics and passed the refraction, SSR, Snell/TIR, mip-roughness and
ambient-height checks. The SP integration test passed on stock `t2_rancor`
and `t3_hevil` with the overlay and an MSVC renderer; it captured the new
height, combined normal, profile and old/new split modes. A six-map runtime
pass with modern water and ambient waves enabled verified body counts and
resolved profiles for `t2_rancor`, `t3_bounty`, `t3_hevil`, `yavin1`, `vjun1`
and generic `t2_trip`. The map-load body construction reported 0–2 ms and
approximately 2–5 KiB of pre-existing body metadata per map. Per-draw wave
setup was not measured separately.

One 1920×1080 Intel UHD Graphics GL 3.2 synthetic run used GPU timer queries.
Four interleaved pairs of 10 full-screen draws measured 14.057 ms for existing
micro-only water and 16.765 ms for quality 1 (2 macro + 2 medium), a 2.708 ms
increment without SSR. A separate quarter-screen sequence measured 3.548 ms
micro-only and 4.252 ms quality 1. Sequential full-screen measurements were
16.612/16.968/18.925 ms at quality 0/1/2; clock drift makes them less useful
for a direct baseline comparison. Two synthetic visible bodies covering half
the screen each in separate draws took 16.747 ms at quality 1. An RTX/AMD-class
result and per-draw CPU timing were not available. No geometry displacement is
enabled, so these are shading-only costs.

The design follows Unreal's per-WaterBody Gerstner asset and depth attenuation
interfaces ([Water Waves Asset](https://dev.epicgames.com/documentation/en-us/unreal-engine/simulating-waves-using-the-water-waves-asset?application_version=4.27),
[Gerstner API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/Water/UGerstnerWaterWaves),
[Water Body wave settings](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-body-actors-in-unreal-engine))
and CryEngine's separation of multi-frequency wave maps and high-spec vertex
displacement ([Water Shader](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1048756)).
