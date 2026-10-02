# Rend2 lens water (`r_rainLens`)

This is optional and off by default. Water on the virtual camera lens covers rain,
spray, splashes and leaving water. It persists under cover and dries gradually.
It isn't a general fluid solver. It's a bounded, art-directable hybrid:

| phenomenon | representation | code |
|---|---|---|
| coherent drops (pin, merge, depin, residual beads, flow heads) | CPU droplet agents, fixed 60 Hz | `tr_lenswater.cpp` |
| thin persistent water (trails, wet paths) | CPU wetness / film field, 30 Hz decay | `tr_lenswater.cpp` |
| violent broad water (downpour, emerging) | a few short art-directed sheets | `tr_lenswater.cpp` |
| optics | one shared lens field + HDR composite | `tr_rainlens.cpp`, `glsl/rainlens.glsl`, `glsl/rainlens_composite.glsl` |

`tr_lenswater.cpp` has no GL or renderer dependencies, so a headless harness can
test it. `tr_rainlens.cpp` gathers the weather and camera input, then rasterises
the state and composites it. It replaced the older procedural hashed-cell drops and
the `r_rainLensSimulation` mass/velocity lattice. The lattice smeared drops into gel,
and the procedural drops vanished under cover.

## Insertion point

`RB_PostProcess` (`tr_backend.cpp`), HDR, after the MSAA resolve:

```
MSAA resolve
[SMAA 2: edges + temporal resolve]   <- history stays drop free
[motion blur]                        <- the world blurs, the lens does not
lens water                           <- RB_RainLens
dynamic glow / bloom extraction      <- displaced lights bloom where they appear
[SMAA 1 edges]
tone map -> sun rays -> glow composite -> debug overlays -> refraction fill
```

When the lens pass runs, it uses the ordering branch that motion blur already
uses. Each effect writes its own target and the next effect reads it (source-FBO
chaining), so nothing copies the scene back. The modern bloom prefilter refracts
the emissive MRT with the same field, which keeps a saber's glow on the displaced
blade. The first legacy dynamic-glow downsample does the same, weighted by the
field's optical weight.

## Frame flow

```
front end  R_AddPostProcessCmd -> R_RainLensInput
           rain intensity + subtype, exposed (R_IsOutside), facing, wind,
           camera water transitions -> postProcessCommand_t.rainLens
back end   RB_RainLensUpdate: game-time clock (cuts clear), gravity projection,
           LensWater::Update  (events, profile, agents 60 Hz, field 30 Hz)
           -> pass skipped unless water is visible or new rain arrives
           RB_RainLens:
             upload film RG (only when dirty) + instance records (RGBA32F)
             film cache: film pass at film resolution, only after an upload
             field FBO: blit of the film cache (or clear), then additive
             instanced quads
             full-resolution composite -> rainLensImage
```

## State (lens space)

Positions are normalised by the screen height: `x = (u − 0.5)·aspect`,
`y = v − 0.5`, y up. Resolution and aspect don't change drop behaviour, and
nothing is stored in pixels.

**Agents** (`LensDrop`: bead, drop, flow head, residual; plus micro drops):
- **Mass and radius.** `m = (r / 0.02)³`, so two equal drops merge to
  `r·∛2 ≈ 1.26 r` with mass and momentum conserved.
- **Pinning (contact-angle hysteresis, artistic).**
  - The drive is `m·|g|` and the pin is `r · noise(p) · lerp(1, 0.4, wet(p))`.
  - A drop starts when the drive exceeds the pin and stops below 0.6 × the pin.
  - The noise is a *stable* spatial value noise. It's never animated, so drops
    meander instead of swimming.
  - A merge increases mass faster than the pinning, so a merged drop depins.
- **Gravity.** World down is projected on the camera right/up axes. Camera roll
  turns the flow, and looking straight up or down leaves almost no tangential
  gravity.
- **Motion.**
  - The acceleration is the excess force, scaled by the profile speed.
  - Drag is 3/s, or 1.6/s for flow heads, with a clamped speed.
  - A weak pull runs along the wetness gradient. Pinning is the main path
    follower.
- **Merge.**
  - The test is O(n²) with contact at `0.85·(ri + rj)`.
  - Moving drops and flows collect micro drops.
  - An impact on an existing drop feeds that drop.
  - After a merge, the survivor and the absorbed lobe relax into one cap over
    180 ms instead of popping.
- **Trails.**
  - Each step loses `1 − exp(−0.006·ds/r)` of mass.
  - The swept segment is stamped once into the field and then only decays, so
    cost follows new activity, not trail lifetime.
- **Residual beads.** `P = 1 − exp(−5·ds)` per step. They take 2–5 % of the
  mass and are placed just outside the merge contact behind the drop.
- **Flow heads.** A drop is promoted when it's larger than 1.25 reference radii
  and faster than 0.22 lens/s. Flow heads deposit more film, get a longer tail
  and collect micro drops from farther away.
- **Impacts.** A drop goes IMPACT (0–150 ms, spread then recoil, lopsided) →
  SETTLING (150–300 ms relax) → SETTLED. A drop can't move while in IMPACT.
- **Evaporation.** The radius shrinks linearly. Per the profile, a reference drop
  dries in `beadLifetime / 0.6`.
- **Caps.**
  - Drops: 48 / 96 / 128 by quality.
  - Micro drops: 256.
  - Sheets: 8.
  - When full, the least valuable state is evicted. The score combines size,
    speed, freshness and peripheral position. Flows score high, so a tiny old
    bead goes first.

**Field.**
- The film/wetness field is `min(screen height, 256)` texels high, 2 floats per
  cell:
  - R = wetness, the path affinity (τ ≈ 12–22 s).
  - G = optical thin film (τ ≈ 2.5–5 s).
- Deposits accumulate, bounded: `v = 1 − (1 − v)(1 − deposit)`. One pass of a
  drop leaves a light trail. Heavy traffic wets the lens further, but never
  past 1. A moving stamp applies only `coverage = ds / 2r` of its amount,
  because the 60 Hz capsules overlap; one pass adds up to about one full
  deposit.
- Decay continues unchanged under cover, and the field is skipped once it's
  empty.
- Stamps write only the CPU field. The 30 Hz field tick marks it dirty, so the
  texture uploads at most 30 times per second, and a trail shows at most 33 ms
  late.
- The surface affinity (pin noise, meander) and a tileable detail noise (the
  splash and emerge structure) are cached per cell at init, so stamps and drops
  sample them instead of hashing.

**Sheets.**
- A sheet is an elongated low-weight ribbon, 0.25–0.8 s long. It moves along
  gravity plus the wind, with a stable wobble, and deposits film.
- They're for heavy rain and splash/emerge events only. There's no
  Navier–Stokes.

## Controller

Exposure only controls *new* water. Under cover, agents keep moving, merging and
evaporating, and the film drains. Nothing is frozen or restored.

- **Weather.** `RE_WorldEffectCommand` records `rainSubtype`: `lightrain` /
  `rain` / `acidrain` / `heavyrain`. The intensity is `particleCount / 5000`.
- **Profiles.** Light, normal, heavy and acid are rows of one parameter table,
  and weather changes crossfade over about 1 s:
  - **Light:** beads and rare merges; no flows or sheets.
  - **Normal:** static beads, moving drops and thin paths at once.
  - **Heavy:** many micro impacts, much more film, many fast flow heads
    (1.6/s) and sheets (1.2/s), shorter bead life, and no more long-lived large
    drops.
  - **Acid:** stickier, with longer film and wet decay.
- **Spawn timing.** Each family (micro, normal, large, flow, sheet) has its own
  Poisson timer. A unit exponential variate is consumed by `rate·dt`, so timing
  is irregular even as the rates change.
- **Rate.** `rate = profile · r_rainLensDensity · exposed · intensityScale · facingTerm`.
  - `facing = −dot(forward, rain direction)`. The rain direction is world
    down, tilted by the weather wind by at most 45°.
  - The facing term depends on the family. Impacts (micro, normal, large) use
    `lerp(0.2, 1, facing^1.5)`, flows `lerp(0.55, 1, facing)` and sheets
    `lerp(0.65, 1, facing)`. A downpour stays heavy when you look at the
    horizon; only the direct hits need a lens facing the rain.
  - `intensityScale = clamp(intensity / nominal, 0, 2)`. The nominal intensity
    of each profile is its standard particle count / 5000: light 0.2,
    normal/acid 0.4, heavy 1.0. The standard weather presets are therefore
    unchanged, and custom particle counts rain more or less. The input
    intensity is clamped to 0..2.
- **Micro impacts.** A rain micro drop starts in the impact state. It spreads
  to about 1.5× with a lopsided shape (more refraction), recoils and settles in
  0.3 s. Satellites thrown by larger impacts land settled.
- **Size.** `r = lerp(min, max, u^β)` with β > 1, so small drops dominate. Heavy
  rain lowers β.
- **Peripheral bias.** Large drops, flows and sheets are rejection-sampled
  toward the edges. Micro and normal drops are uniform.

**Events** (`LensWaterEvent`, at most 8 queued per update):
- Events run after the profile blend and carry their own presets (film, sheet
  and flow speed), independent of the active weather. Emerge uses film 1.0,
  sheet speed 1.4 and flow speed 1.0; splash uses film 0.6.
- `SPLASH` gives a burst of drops, one or two large impacts, micro drops and
  local film.
- `SPRAY` gives repeated small impacts for its duration, biased toward a side.
- `EMERGE` is a transient of about 2.5 s, not a burst:
  - t = 0: an uneven film over the whole lens, with thick patches, thin holes
    and fairly sharp edges; the refraction comes from the thickness gradient.
    Also 2–4 broad sheets.
  - 0.1–0.5 s: 2–5 flow heads start upstream and run with gravity.
  - 0.4–1.5 s: narrower rivulets and one or two late thin sheets.
  - 0–2.5 s: the film tears, and holes open and grow where the detail noise is
    low.
  - No beads are spawned. The residual mechanism leaves them behind the flows.
  - A second emerge within 0.5 s only raises the strength.
- `SUBMERGE` and `CLEAR` wipe everything.

A continuous spray input (map emitters) spawns like a spray, with no events. It
works without rain and under cover.

- **Acid:** also a slight green-yellow transmitted tint `(0.93, 1, 0.85)` and 1.15×
  distortion. Both crossfade with the profile; the distortion also applies to bloom.
- **Inertia** (`r_rainLensInertia`, default 0):
  - The camera acceleration (the view origin, differentiated twice over game time,
    filtered at 50 ms, reset on teleports and cuts) acts as extra gravity:
    `g += −a · 2e-4 · r_rainLensInertia`, clamped to 1.5.
  - Water lags behind a strongly accelerating camera and stays still for normal
    movement.

## World water (events, emitters, scripts)

Everything the world throws onto the lens goes through one front-end queue in
`tr_rainlens.cpp`:
- `R_RainLensInput` resolves it against the main view once a frame
  (`lenswater::ResolveWorldEvent`):
  - Distance falloff `(1 − d/radius)²`.
  - Facing `lerp(0.3, 1, dot(forward, toSource))`: water from behind the camera
    reaches it less.
  - Lens side from the direction to the source.
- Events are dropped while the camera is under water.

**Renderer extension.** `GetRefLensWaterAPI` → `AddLensWaterEvent(const refLensWaterEvent_t *)`
is declared in both `tr_public.h`. `refLensWaterEvent_t` is in both `tr_types.h`:
- `type`: `LENSWATER_SPLASH/SPRAY/EMERGE`.
- `flags`:
  - `LENSWATER_F_ORIGIN`: origin and radius give the falloff and side.
  - `LENSWATER_F_DIR`: dir is the world direction of the water, which comes from
    the opposite side.
  - `LENSWATER_F_LOCAL`: the viewer itself.
- Also `origin`, `dir`, `radius`, `strength` (0..2) and `duration` (s, spray).

It's safe at any time and ignored while `r_rainLens` is off.

| caller | path |
|---|---|
| SP cgame | `cgi_R_AddLensWaterEvent` → `CG_R_ADDLENSWATEREVENT` (only with `cl_rendererLensWater`, which the engine sets when the renderer exports the extension) |
| MP cgame | `trap->ext.R_AddLensWaterEvent` (engine wrapper; a no-op on a legacy VM or another renderer) |
| MP efx | the engine FX calls `reLensWater->AddLensWaterEvent` directly |

**cgame hooks** (`CG_LensWaterEvent`, SP `cg_event.cpp`, MP `cg_event.c`):
- `EV_WATER_TOUCH` of any entity: a SPLASH at its origin, radius 200, strength 0.7
  (0.5 for the viewer).
- `EV_WATER_LEAVE`: radius 160, strength 0.45 (0.3 for the viewer).
- The viewer's `EV_WATER_CLEAR` (the head leaves water, the game's pmove `waterlevel`)
  in first person: `EMERGE | LOCAL`. The strength is 0.6..1.2 from the time since
  `EV_WATER_UNDER`.
- The renderer's own contents test (`CONTENTS_WATER | CONTENTS_SLIME` at the camera,
  under ≥ 250 ms) remains the fallback for third person and older games. Whichever
  emerge comes first within a second wins, so there is one burst only.

**EFX primitive `lensWater`** (SP cgame FX, MP engine FX), a one-shot like
`cameraShake`:
```
lensWater
{
	lensEvent	spray		// splash | spray | emerge
	intensity	0.8			// strength (a range works)
	radius		320			// reach from the effect origin, 0 = everywhere
	life		1500		// spray duration, ms
}
```
A waterfall uses an `fx_runner` (or a looping effect) with a `repeatDelay` a
little shorter than `life`. Bolted effects use the entity origin (SP); MP bolted
lens water isn't supported, like cameraShake.

**Map emitters** in `cubemaps/<map>/env.json`, next to `FogVolumes`:
```json
"LensWaterEmitters": [
	{ "Origin": [ 1024, -512, 96 ], "Radius": 400, "Strength": 1.0, "Type": "spray",
	  "Direction": [ 0, 0, -1 ] },
	{ "Origin": [ 300, 80, 0 ], "Radius": 250, "Strength": 0.8, "Type": "splash", "Interval": 3 }
]
```
- `spray`: while the camera is within the radius, the strongest emitter feeds the
  continuous spray (falloff, facing, side as above).
- `Direction` (optional): only water travelling toward the camera reaches it.
- `splash`: fires at exponential intervals (mean `Interval` s, game time).
- Up to 64 emitters are allowed.

**Scripts and console.** `r_we lenswater <splash|spray|emerge> [strength] [duration] [x y z radius]`
is the `RE_WorldEffectCommand` branch (it doesn't reload the weather images). It also
works from:
- SP `cgi_R_WorldEffectCommand`;
- MP `trap->R_WorldEffectCommand`;
- ICARUS through a console command.

Don't route repeated server events through `CS_WORLD_FX` configstrings: they're
deduplicated and replayed on load.

## Optics

- **Film pass** (`USE_FILM`, fullscreen into `rainLensFilmFieldImage`, field
  resolution RGBA16F, so the blit is 1:1). It runs only when the film was uploaded (30 Hz at most)
  or `r_rainLensFilm` changed; every frame a bilinear blit copies the cache
  into the field, so the five-tap gradient doesn't run per display frame.
  - `offset = ∇film · 0.035 · r_rainLensFilm`.
  - `weight = min(0.45·film, 0.25) · r_rainLensFilm`.
  - No blur.
  - A trail refracts slightly and is never an opaque stripe. With no visible
    film, the field is cleared instead.
  - Film-first model (`r_rainLensFilmModel 1`, see below) instead:
    `offset = ∇film · 0.035 · r_rainLensFilm + cov · micro · r_rainLensFilm ·
    (0.0025 ∇N₂₂ + 0.0012 ∇N₆₅)` and `weight = 0.9 · cov · min(r_rainLensFilm, 1)`,
    with `cov = smoothstep(0.02, 0.3, film)`. N₂₂ and N₆₅ are static quintic value
    noise octaves of 22 and 65 cells per screen height, in lens space, with an
    analytic gradient. Evenly wet glass refracts everywhere instead of only at
    thickness edges.
- **Drop pass** (`USE_DROPS`, instanced 4-vertex strips, records fetched by
  `gl_InstanceID`, additive blend):
  - A drop is a spherical cap in its own frame. It's stretched behind along
    the motion (tail length `1 + 3|v|`, clamped) and lopsided by seed and
    impact state.
  - The analytic slope becomes `offset = −slope · r · 1.6 · mask`.
  - The merge lobe adds its own cap, so overlapping heights sum their slopes
    and normals don't switch hard.
  - Sheets use an analytically differentiated ribbon profile (6 trig
    operations instead of 15): strong offset, weight ≤ 0.2, A = 0.0025 inside
    the sheet.
  - Only covered pixels run, so the cost doesn't scale with field size ×
    procedural work.
- **Field.** RGBA16F, 256 / 360 / 540 high by `r_rainLensQuality` (or
  `r_rainLensFieldHeight`, clamped to the screen). It holds RG = unscaled UV
  offset, B = weight and A = the physical blur radius `r · 0.18 · mask`,
  before `r_rainLensBlur`. It's additive, so consumers clamp B and A.
- **Normal reconstruction.** The composite recovers the drop slope as
  `offset / (A · 1.6 / 0.18)`: radius and mask cancel, so a large drop isn't
  steeper and a soft edge isn't flattened. Film writes no A and falls back to
  the reference drop edge offset 0.032; `r_rainLensBlur` applies in the
  composite.
- **Composite** (full resolution, 2 variants: `USE_CUBEMAP` or not):
  - A pixel with weight ≤ 0.001 costs one field fetch and one scene fetch.
  - Water adds one refracted fetch, and blur taps run only where the blur
    radius is above zero: 1 / 3 / 5 scene samples by quality. With
    `r_rainLensMipBlur`, it's one fetch from a mipped half-resolution copy.
  - The refracted scene is multiplied by the profile tint.
  - **Fresnel** (water, n = 1.333, F0 = 0.02, Schlick on the drop normal):
    `water = (1 − F)·refracted + F·reflection`. At the drop rim, reflection takes
    over. Thin film is nearly flat, so it reflects at about F0, takes the
    ambient reflection instead of the cubemap and gets no glint. Cubemap
    fetches and glints are branched on compact drops (weight above the 0.25
    film cap) and steep sheets (n.z < 0.97). In the film-first model the film's
    own weight (recomputed from the raw film texture) is subtracted first.
  - `r_rainLensReflection` scales the Fresnel once, `F' = clamp(F · amount, 0, 1)`.
    The reflection sources and glints are physical (before 2026-10-01 the
    amount was applied to both, so 0.5 gave about a quarter).
  - **Reflection:**
    - The camera's nearest environment cubemap (`R_CubemapForPoint`, roughness mip
      ≈ 0.35 of the chain, defocused). It's sampled with the drop's reflection
      vector rotated to world space, and never brighter than the light grid at
      the camera (like lightall).
    - Without a cubemap (`r_cubeMapping 0`, no probes), a sky/ground ambient from
      the light grid.
  - **Glints** (compact drops only, weighted by F):
    - The dominant light: the sun when the sky shader has one and the camera is
      outside, else the light grid direction at the camera at half strength.
      Radiance is the light grid directed light at the camera.
    - The brightest nearby dynamic light (sabers, muzzle flashes; `luma · falloff`
      within 2 × radius).
    - Normalised Blinn-Phong with powers 220 and 120.
    - There's no fixed screen-space light any more.
  - Everything is gathered on the front end (`rainLensInput_t`, light grid via
    `R_LightForPoint`), so the composite is branch-light. It's skipped under
    water and on a dry lens with no events, spray or exposed rain.
  - `r_rainLensReflection 0` gives pure refraction.

## Cvars

| cvar | default | |
|---|---|---|
| `r_rainLens` | 0 | latched, needs `r_hdr`; off = no targets, no simulation |
| `r_rainLensQuality` | 1 | latched: field 256/360/540, drops 48/96/128, samples 1/3/5 |
| `r_rainLensDensity` | 1.0 | rain input 0..4 |
| `r_rainLensDropSize` | 1.0 | geometry only (render, merge contact, trail width); rain amount unchanged |
| `r_rainLensRefraction` | 1.0 | UV distortion 0..4 |
| `r_rainLensFilm` | 1.0 | thin film / trail visibility 0..2. The film weight is capped at 0.25, so above 1 only the refraction grows (the composite treats weight > 0.25 as a compact drop with rim and glint) |
| `r_rainLensBlur` | 1.0 | drop defocus 0..2 |
| `r_rainLensReflection` | 1.0 | reflection + glints 0..2, 0 = refraction only |
| `r_rainLensInertia` | 0 | camera acceleration response 0..2 |
| `r_rainLensFilmModel` | 0 | A/B: 0 = hybrid drops (A), 1 = film-first (B), see "Film-first model" below |
| `r_rainLensEmergeWater` | 1.0 | film-first: water left after leaving water, 0.25..3 (bead count ×, size ×cbrt, rim spacing /sqrt) |
| `r_rainLensEmergeTime` | 1.0 | film-first: time scale of draining, tearing and the rim breaking up, 0.25..4 (below 1 = drops sooner) |
| `r_rainLensMist` | 0.04 | film-first: rain flux floor outside when not looking down (spray, mist), 0..1 |
| `r_rainLensImpactSplat` | 0 | film-first: oblique rain impacts splat along their travel, throw spray forward and leave a smear, 0..2 (0 = round impacts) |
| `r_rainLensFilmMicro` | 1.0 | film-first: film micro refraction, 0..4 |
| `r_rainLensPeripheralBias` | 1.0 | keep large drops (and film-first emerge beads) away from the centre, 0..2 |
| `r_rainLensDebug` | 0 | cheat, forces the pass on: 1 weight/blur, 2 normal, 3 offset ×40, 4 scene/final split, 5 agents (pinned blue, moving green, flow red, residual yellow, micro grey, sheet magenta), 6 film green / wetness blue, 7 pin ratio (blue pinned → red depinning), 8 transient (impact yellow, settling orange, forming green, merge lobe cyan, sheet magenta), 9 controller panel |

Debug view 9 draws bars in the top-left corner:
- Cyan: intensity, exposed, facing, map spray.
- Green: blended micro / normal / large / flow rates.
- Orange: sheet rate, active sprays, the event flash (red, 0.3 s after an event),
  drops / limit.
- Then the profile swatch, with a white edge when forced.

Developer cvars (cheat):
- `r_rainLensFieldHeight` (latched, 0 = quality)
- `r_rainLensAgentLimit` (8..128, 0 = quality)
- `r_rainLensPinning`, `r_rainLensMerge`
- `r_rainLensFilmDecay`, `r_rainLensWetDecay`
- `r_rainLensHeavyFlow`
- `r_rainLensPBO`: film and instance uploads through a 3-slot pixel buffer ring.
- `r_rainLensMipBlur` (latched): drop defocus from a mipped half-resolution scene
  copy (a blit plus `glGenerateMipmap`) instead of the 3/5 taps.

The last two are profiling options from the design doc. They're off until
measurements favour them.

## Film-first model (`r_rainLensFilmModel 1`)

Water on the lens is a film first; drops are what the film breaks into.
Everything below applies only to model 1, so model 0 stays as the reference.

- **Micro drops.** They live for a per-profile `microLifeMin..Max`: light
  1.5-3 s, normal 1-2, heavy 0.5-1, acid 1.2-2.4, ×0.6 at the screen centre.
  They fade over the last 30 % of that time and hand their water to the film.
- **Film.** It refracts through static micro-lens noise (see Optics) and is
  weighted by its coverage.
- **Beads at rest dry.**
  - `restAge` is the time since the drop last moved faster than 0.01 or merged.
  - Beads, residuals and normal drops dry at `kDryRate` (about a second) once
    `restAge` exceeds `beadLifeMin..Max`. Those bounds are light 6-10 s,
    normal 4-8, heavy 2.5-5 and acid 5-9.
  - The lifetime is ×0.5 at the centre and ×0.7..1.5 by size.
  - A drying bead leaves a small film stamp.
- **Forming.** `STATE_FORMING`: beads that dewet from the film grow over
  250 ms (radius 0.3 → 1, weight too). Residuals pinch off the same way from
  0.1 s, and rain flow heads grow in while they run.
- **Emerge** (`StartFilmEmerge` / `UpdateFilmEmerge`) follows a film lifted out of water. There are no sheets and no flow heads.
  - **Drainage.** The film drains like Jeffreys' similarity solution, `h = sqrt((s + 0.206) / (t + 0.3))`. Here s runs from 0 at the top to 1 at the bottom along the gravity at the moment of emerging, and h is normalised so the bottom starts at 1. The film thins everywhere, stays thickest at the bottom and never forms a front ([Jeffreys / Reynolds thinning](https://keele-repository.worktribe.com/OutputFile/466561)).
  - **Streaks.** Drainage streaks are noise stretched along the gravity (18 × 3 cells per height, plus a second octave). They modulate h by 0.8..1.2.
  - **Rupture.** A cell tears to a 0.05 trace (tau 0.2 s) once h falls below `0.28 · (1 ± 0.17)`. The top tears at about 0.35 s and the bottom at about 3.5 s.
  - **Dewetting sites.** There are 10-20, sitting on the thicker spots with peripheral rejection. Their times come from the analytic rupture time, so the top goes first. Their sizes are rn 0.2-0.45, growing down the lens.
  - **Rim.** A rim (`s > 0.93`) holds the film until 1.2 s, then breaks into a row of beads along the bottom edge: spacing 0.12-0.18, rn 0.45-0.9, Rayleigh-Plateau style.
  - **Drift.** The film micro structure drifts down with the water: `FilmFlow = 0.04 · ln(1 + t / 0.2)` along the gravity, frozen afterwards. It is fed to the film pass as `u_RainLensParams2.xy`.
- **Rain flux.** `R_RainLensInput` works out the camera velocity, filtered over 0.1 s and reset on cuts. From it, the flux is `max(0, −dot(v_rain − v_cam, forward)) / 500` with `v_rain = fall · 500 u/s`, wind tilted. It is clamped to 0..2, with a 0.04 mist floor outside unless the camera looks down.
  - Looking up into vertical rain gives 1, looking level while standing 0, running into it more, and looking down nothing.
  - Every rain rate of the film-first model (impacts, flows, sheets, `filmRain`) scales with it, with no minimum. Impacts grow up to ×1.2 at high flux.
  - The hybrid model keeps `facing`.
- **Oblique impacts** (`r_rainLensImpactSplat`). An impact takes milliseconds, far less than a frame, so what shows is its result:
  - The obliquity is `|slant| / (|slant| + flux)`. `slant` is the relative rain velocity across the lens plane, in lens space and over 500 u/s (`RainFlux`).
  - Splat = obliquity · cvar. Within 0.3 s it stretches the impact (tail up to 2.5×) and shifts it forward along the travel; the direction is scattered by ±20°.
  - Drop impacts throw `cvar · flux · 2` spray micro drops in a ±35° fan ahead, plus a smear capsule of film.
  - Micro impacts stamp a stretched capsule of about the same area.
  - Head-on impacts (looking up into vertical rain) stay round.
- **Drop size.** The drawn geometry is ×0.6 by default (`p.dropSize`); `r_rainLensDropSize` multiplies on top.
- **Rain population.** It uses the `FilmFirstTable` profiles: micro ×1.3,
  normal ×0.5, no large impacts, sizes 0.2-0.7 ref, beta 2.5.
  - Impacts feed a drop within 1.5 × its radius, so beads grow by accretion
    and merging.
  - A downpour lays down film directly: `filmRain` per second is light 0,
    normal 0.01, heavy 0.2 and acid 0.015, modulated by the detail noise.
- **Sliding.**
  - Drag is `kDrag · 1.5 / max(rn, 0.6)` (×1.2 for flows), so speed grows
    with size: rn 1.2 ≈ 0.06, rn 2 ≈ 0.26 lens/s.
  - Wetness and defects are sampled at the advancing contact line (1.2 r
    ahead), not under the drop's own fresh trail.
  - Sparse sticky defects (40 cells per height, up to ×2.5 adhesion) stop
    drops just above their depinning size until rain or a merge feeds them.
- **Glints.** They're gated by drop radius (`lens.w / 0.18`, smoothstep
  0.008 → 0.02), so micros and small beads have none. Powers are 70 / 50 at
  ×2.5, capped per channel at 1.5 × the light radiance.

## Commands

- `rainlens_clear` clears drops, sheets and film.
- `rainlens_event splash|spray|emerge [strength] [left|right|up|down]` triggers
  an event for manual testing (lens space).
- `rainlens_profile light|rain|heavy|acid|auto` forces a profile. Without
  active weather, a forced profile also rains on the lens everywhere, for
  tuning on dry maps.
- `rainlens_stats` prints:
  - counts by type and the blended rates;
  - sprays, map spray, time since the last event and map emitters;
  - field and film sizes and state, and the PBO mode;
  - CPU µs: update, agents, field, events, upload;
  - GPU ms of the field and composite (its own timestamp ring, read without
    stalling, independent of `r_speeds`). Timestamps and the upload timing are
    taken on the frames the core measures (debug, `r_speeds`, every 16th);
  - the reflection source and the key / nearby light levels.

`r_speeds 100` also shows "Rain lens field" and "Rain lens composite".

## Cost

A headless harness (`tools/lenswater_harness`, MSVC `/O2`) tests the CPU core:
- Heavy rain at density 2, at the caps, costs about 0.2 ms per update.
- The 116k-cell field decay is branch-free.
- A dry lens under cover costs almost nothing. With no agents, no sheets, no
  visible film, no events, no emerge, no spray and no incoming rain, `Update`
  returns at once (no profile blend, no steps) and the pass is skipped.
  Invisible wetness left behind sleeps: the time is accumulated, and on wake
  one `exp(−t/τ)` decay is applied, so old paths age correctly at no idle
  cost.
- Direct micro impacts deposit a trace of film (0.025–0.075, 2.5 × radius),
  so a downpour wets the lens over time through the bounded accumulation.
- The emerge breakup reads a per-cell pattern built once at the emerge (no
  modulo in the 30 Hz loop).
- CPU timings (`steady_clock`) are taken only with `r_rainLensDebug` or
  `r_speeds` on, or every 16th frame for `rainlens_stats`.
- The film uploads only on 30 Hz field ticks that changed it.
- The optics add, per water pixel only, one cubemap fetch and two glint terms.

## Validation (in game, not yet done)

Harness-verified (`tools\lenswater_harness\build.bat`):
- A single bead stays pinned and a large drop slides.
- A merge gives r ≈ 1.26 r, conserves mass and depins.
- Trails leave film and wetness; residuals appear.
- Roll turns the flow and a vertical view pins.
- Under cover there are no spawns, drops keep moving, the film decays and beads
  remain.
- Heavy rain has many more flows and sheets without more large static drops.
  Light rain has no sheets.
- Caps hold under emerge/splash spam, and the lens dries completely.
- Emerge spawns no beads at t = 0 and starts with film and sheets. Flow heads
  follow, the film tears, and residual beads appear.
- One pass leaves an unsaturated trail; repeated passes accumulate, bounded.
- Intensity scales the rate; a horizontal view keeps most heavy flows and
  sheets; micro impacts have an impact phase.
- The film goes dirty only on field ticks, and a dry update returns at once.
- Invisible wetness decays over the dormant time on wake; micro impacts leave
  film.
- The spray emitter wets its side and stops out of range.
- World event falloff, facing and side.
- Inertia only when enabled.
- The acid tint and refraction.

In game:
- Single drop, merge and trail on a bright scene (debug 5/6/7); debug 9 while
  switching `rainlens_profile`.
- Roof test: 5 s under cover, then back out.
- Camera roll, looking up and down.
- `heavyrain` should read as turnover, film and rivulets, not 2.5× the drops,
  also when looking at the horizon.
- Leaving water (debug 6 / 5, then the normal view): a wet, uneven lens that
  tears into sheets and streams, with beads left behind, not a scatter of
  drops.
- Leave a pool in first and third person (one burst each).
- An NPC jumping into water nearby (splash from its side).
- `r_we lenswater splash 1`, an env.json spray emitter, and an efx `lensWater`
  primitive.
- Reflections:
  - on a map with cubemaps versus `r_cubeMapping 0`;
  - the sun glint while turning toward / away from the sun and under a roof;
  - a saber glint.
- Saber behind a drop with `r_bloom 1` (the glow follows the refracted blade).
- `r_smaa 2` (no drop ghosting), `r_motionBlur`, MSAA.
- No effect in mirrors, portals, sky portals, cubemap bakes and UI models.
- `r_speeds 100` / `rainlens_stats` at 1080p, 1440p and 4K, with and without
  `r_rainLensPBO` / `r_rainLensMipBlur`. The field should stay flat; only the
  composite should scale.
