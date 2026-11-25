# Rend2 FX upgrades

Four opt-in upgrades of the legacy efx system (CPU sprite primitives with linear
start → end parameters). Every feature is **off by default**; with all four off
nothing is classified, no program is compiled and no resource is created, so the
image is the same as before. Soft particles and transparency sorting are separate
work (`docs/rend2-particles.md`).

| Cvar | Default | Meaning |
| --- | --- | --- |
| `r_fxLights` | 0 | Emissive FX sprites light the scene as Forward+ point lights (needs `r_forwardPlus 1`) |
| `r_fxLightsMax` | 16 | Most FX lights per frame (brightest merged clusters) |
| `r_fxLightsScale` | 7 | Light radius per √(sprite power) |
| `r_fxLightsMerge` | 64 | Sprites closer than this (units) share one light |
| `r_fxDistortion` | 0 | Heat haze / shock distortion of fire, explosions, muzzle flashes; latched |
| `r_fxDistortionStrength` | 1 | Refraction amount |
| `r_fxDistortionMax` | 96 | Most distorting sprites per frame |
| `r_fxGpuParticles` | 0 | GPU secondary sparks / embers (GL 4.3 compute, `r_gl43 1`); latched |
| `r_fxGpuParticlesDensity` | 1 | Secondary particle count multiplier |
| `r_fxLayering` | 0 | Missing layers of stock impacts / explosions; latched |

Each has `r_<feature>Compare` (0 normal, 1 split screen, 2 off without
`vid_restart`): `r_fxDistortionCompare` and `r_fxGpuParticlesCompare` split the
screen; `r_fxLightsCompare` and `r_fxLayeringCompare` have no split (a light or a
decal belongs to the whole scene) and only switch off.

Code: `shared/rd-rend2/tr_fxupgrades.cpp`, `glsl/fx_distortion.glsl`,
`glsl/fx_particles.glsl`, `glsl/fx_particles_sim.glsl`,
`shared/fx/FxLayering.h` (event classification, shared by SP and MP).

## 1. FX lights (`r_fxLights`)

In `RE_RenderScene`, before the scene takes its lights, the world scene's FX
primitives (`RT_SPRITE`, `RT_ORIENTED_QUAD`, `RT_LINE`, `RT_ELECTRICITY`,
`RT_CYLINDER`, excluding first-person / depth-hack ones) are scanned:

- **Classification** per material (cached): additive blend (`ONE ONE` or
  `SRC_ALPHA ONE`) and not smoke / dust / steam / water / glass / blood. The
  radiance of stock fire materials comes from the fx_physicalization emission
  table (`FxPhysicalizationEmissionStock.h`); other hot materials (fire, flash,
  muzzle, spark, blaster, bolt, glow, flare) use 0.18, the rest 0.08.
- **Power** of a sprite = radiance × linear luminance of its RGB (additive
  sprites carry their fade in RGB) × size². White-tinted fire sprites get a
  flame chromaticity.
- **Merge**: brightest first, a sprite within `r_fxLightsMerge` of a cluster
  joins it (power-weighted position, summed colour).
- **Budget**: the `r_fxLightsMax` most powerful clusters. Clusters next to an
  authored scene light (efx `Light` primitive, saber, etc.) are skipped, so effects
  that already light the world are not doubled.
- **Intensity**: inverse-square falloff means the radius where the irradiance
  drops to the dlight threshold grows with √power:
  `radius = clamp(r_fxLightsScale·√power, 24, 512)`. The light goes through
  `R_AddSceneDynamicLight`, so `r_pbrPhysical` converts it to candela like any
  other dynamic light. Radii are smoothed with the previous frame's clusters
  against sprite birth/death flicker.
- **No shadows**: `dlight_t::noShadow` (new), honoured by `R_DlightCastsShadow`
  (Forward+ shadow budget, legacy and ray traced paths).

Only active with Forward+ (`R_ForwardPlusActive`), where many cheap lights are
affordable.

## 2. Heat haze / distortion (`r_fxDistortion`)

Front end: FX primitives whose material name marks them as fire / flame / burn
/ heat (strength 0.6), explosion / blast (1.0), muzzle / flash (0.45) or shock /
ring / wave (a ring) become distortion sources (centre, radius = 1.6 × size,
strength × visibility). The `r_fxDistortionMax` best by strength × radius /
distance are kept. `r_fxLayering` adds timed sources (explosion shock rings,
muzzle heat puffs).

Back end (`RB_FxUpgradesView`, end of the main pass of the main view, after
every transparent surface):

1. the scene depth is copied (resolves MSAA) for soft occlusion;
2. **offset buffer**: half resolution RGBA16F, instanced camera-facing quads
   added together. Heat = two octaves of value noise scrolling upward × a radial
   mask; shock = a Gaussian ring travelling out with the age. The amplitude
   scales with the projected size of the source and fades when the source is
   behind the depth (sources behind walls refract nothing);
3. **composite**: one full-screen pass reads a resolved copy of the scene at
   `uv + offset`; pixels without offset are discarded (MSAA edges and the glow
   attachment elsewhere stay untouched).

The existing `RF_DISTORTION` / `useDistortion` refraction fill path is left as
it is: it refracts per surface (glass, force effects); this pass is a
screen-space field for FX without geometry.

## 3. GPU secondary particles (`r_fxGpuParticles`)

A pool of 8192 particles in an SSBO, simulated by one compute dispatch per frame
(`fx_particles_sim`, GL 4.3: `r_gl43 1`, compute + SSBO + image load/store).
Without those the programs are not built, the pool is never created, a line is
printed, and the feature does nothing.

- **Spawning**: the FX scheduler posts one event per played effect
  (`GetRefFxEventAPI`, below). Impacts on metal (12 sparks), energy impacts (6),
  explosions (40 sparks + 12 embers), spark showers (16) and fires (1 ember per
  play, fires replay continuously) add secondary particles × density. The CPU
  owns the ring head: this frame's particles are uploaded and the kernel writes
  them into slots `[head, head + count)`, no atomics.
- **Motion**: gravity (sparks 1 g = 800 u/s², embers slightly buoyant, dust
  floating), exponential drag, divergence-free curl noise (curl of a two-octave
  sine potential).
- **Collision**: the particle is projected into the depth copy; when it is just
  behind the visible surface, the surface position and normal are reconstructed
  from three depth taps (inverse view-projection), the velocity is reflected
  with the particle's bounce (tangent friction 0.7) and the particle is placed
  on the surface. Embers lose life on contact, dust settles.
- **Draw**: one instanced quad per slot from the state image. Sparks / embers
  are velocity-stretched capsules with HDR emission (energy conserved over the
  streak length), added; dust is a round premultiplied puff lit once at its
  spawn from the light grid. Soft depth fade against the same depth copy (as
  the soft particles do). Dispatch and draw are skipped once every spawned
  particle has died.

## 4. Auto layering (`r_fxLayering`)

`shared/fx/FxLayering.h` classifies an effect by its path (impact metal / stone
/ energy, explosion, muzzle, fire, sparks; screen, water, rain, saber, force and
blood effects are never touched) and records what its template already contains:
a `Light`, a `Decal`, spark streaks (`Line` / `Tail` / `Electricity` or a
`spark` group), dust / smoke / debris groups, a `Cylinder` or shock group. Only
missing layers are added:

| Class | Added when missing |
| --- | --- |
| impact metal | sparks, light, scorch |
| impact stone | dust, light, scorch |
| impact energy | sparks, dust, light, scorch |
| explosion | sparks, dust, light (380 u, 450 ms), shock ring (260 u, 550 ms), scorch (40 u) |
| muzzle | light (160 u, 70 ms), heat puff |

Sparks and dust use the GPU pool (GL 4.3), lights the Forward+ list
(unshadowed, quadratic fade), the ring the distortion pass, scorch marks the
renderer's decal system (`gfx/damage/burnmark4`, else `burnmark1`).

### Event API

`refFxEvent_t` / `GetRefFxEventAPI` (`tr_types.h`, `tr_public.h`, SP and MP):
`AddFxEvent(event)` and `FxEventsWanted()`. MP: the client FX scheduler
(`codemp/client/FxScheduler.cpp`) calls it directly. SP: the cgame
(`code/cgame/FxScheduler.cpp`) uses the new syscall `CG_R_ADDFXEVENT`, appended
to `cgameImport_t`, and only when the engine set `cl_rendererFxEvents 1` and
`r_fxGpuParticles` or `r_fxLayering` is on (older engines and other renderers
are unaffected).

## Validation

- SP + MP engines, SP game and both rend2 DLLs build with the w64devkit
  toolchain.
- `python tools/rend2/test_fx_upgrades_gl.py` (hidden SDL context): compiles and
  links every program on Intel UHD and on the NVIDIA RTX 2060
  (`SHIM_MCCOMPAT=0x800000001`), and runs the simulation kernel: ring spawn
  writes only its slots, a spark falling onto a depth-buffer plane is reflected
  and stays in front of it, gravity, life countdown.
- Defaults: every path is gated by the cvars; the only always-present change is
  the zero-initialised `dlight_t::noShadow` and two uniform table entries.
- In game: see the status at the end of the hand-over notes; visual tuning
  (light scale, distortion strength, spark counts) has not been judged in play.
