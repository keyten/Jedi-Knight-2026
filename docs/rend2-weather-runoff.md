# rend2 water runoff (`r_weatherRunoff`)

A thin water film with streams running down rain-exposed slopes and walls. It is a shading feature only. It changes
roughness, the normal and a little albedo before any lighting. There is no fluid simulation, no flow map, no extra pass
and no new depth map. It needs `r_weatherWetness` (the `USE_WETNESS` lightall path) and a raining map.

## Where it runs

`lightall.glsl`, in the `USE_WETNESS` block, right after the puddle / ripple code. Like the puddles, it only changes
`N`, `roughness` and `diffuse`, so direct light, Forward+ / legacy dynamic lights, cubemap IBL, SSR and SSGI all see
it. There is no separate reflection code.

## Slope classification

From the geometric normal `Ng` (`c = Ng.z`). All the weights are smooth:

| class | weight |
|---|---|
| flat, facing up → puddle | `puddleSlope = smoothstep(slopeMin, slopeMax, c)` (`r_puddleSlope`, default 0.90 0.98) |
| sloped / near vertical → runoff | `runoffW = 1 - puddleSlope`, the exact complement |
| facing down → none | `× smoothstep(-0.35, 0.05, c)` (ceilings 0, slightly overhanging faces reduced) |
| steep (wall probe, debug only) | `1 - smoothstep(0.35, 0.7, c)` |

With the defaults a 10° slope is a puddle candidate, 30°, 60° and vertical faces are runoff, and the range in between
blends. Puddles are only on world geometry, while runoff can also be on entities (`r_runoffEntities`).

## Gravity projection

```
G    = (0, 0, -1)
flow = G - Ng * dot(G, Ng)            // downhill in the tangent plane
m    = |flow| = sin(slope)
F    = m > 0.05 ? flow / m : 0
runoffW *= smoothstep(0.05, 0.2, m)
```

`F` drives the debug view. The pattern gets the same direction without using `F` itself, as the next section shows.

## Procedural flow pattern

On any plane the downhill direction is perpendicular to the contour lines, and contour lines are horizontal. So a field
`g(across, z)`, where `across` is a horizontal coordinate along the contour and `g` is stretched along world `z`, runs
along projected gravity on every slope. A pattern that moves toward lower `z` can never flow upward. It depends only
on the world position, so it does not swim with the camera and has no UV seams. Texture V is never used.

A per-pixel rotation of `across` would distort badly at large world coordinates on curved rock. Instead the contour
angle (mod 180°) picks the nearest of **8 fixed horizontal axes**, 22.5° apart (`RunoffPattern` / `RunoffBin`):

* walls are exact with any horizontal axis, because their flow is straight down;
* slopes are off by at most 11°;
* 70 % of directions use 1 evaluation. The rest blend 2 bins continuously, so rounded rock and pillars have no seams.

`RunoffStreaks(across, along, clock)`, in pattern cells (`r_runoffScale` world units), uses 5 value-noise evaluations:

* a low-frequency warp of `across`, so the streams wiggle;
* static channels at two widths (`smoothstep`-thresholded noise, stretched 11× and 17× along the flow). Water keeps
  its paths;
* two pulse layers that run down the channels at `r_runoffSpeed` and `r_runoffSpeed / 1.7`, so the result never reads
  as one uniformly scrolling texture;
* `film = (0.25 + 0.75 stream) × pulse`: a thin sheet everywhere, full in the streams. `core = stream × pulse`.

The CPU integrates the clock once per frame (`clock += min(dt, 0.1) · speed / scale`) and wraps it at 256 cells. The
moving layers' lattice repeats every 256 cells along the flow (`RunoffNoise`), and the clock enters with an integer
factor, so the wrap is seamless and the floats stay precise.

**Wind** (`weatherSystem->windDirection`) shears `across` with the fall: `across + shear·z`, with shear up to 0.35
(≈20°). Streaks lean downwind but still run down. Faces toward the wind run up to 30 % more, and sheltered faces less.

**Entities** (`r_runoffEntities 1`): the pattern frame is the entity origin plus its yaw axis
(`u_RunoffFrame`, `u_RunoffParams2.w`). Movers, props and characters that translate or turn keep the pattern glued to
them, and streaks stay world-vertical. Limitation: skinned limbs can slide a little under the pattern, and tilted props
only re-derive the yaw.

## Rain-exposure limitation

`weatherDepth` is a **top-down** map. It is right for vertical rain on surfaces that face up. It does not know about
wind-driven rain hitting a wall. Runoff uses the existing exposure conservatively (`smoothstep(0.25, 0.9, exposure)`).

Cheap approximation for steep faces: a second exposure test further out along `Ng` (`r_runoffProbe` texels, default
1.5, at most 32 world units), weighted by steepness: `exposure = max(base, steep × probe)`. An exterior wall next to
open sky then counts as exposed, instead of getting the half-occluded bilinear result at its own top. A wall under a
roof deeper than the probe stays dry. Walls under eaves shallower than the probe get wet, which is near-physical with
wind. There is no directional weather map. If the probe proves unstable in game, `r_runoffProbe 0` limits runoff to
what the top-down map sees. Debug 24 shows where the probe added exposure, in red.

## PBR response

* normal: `N = mix(N, Ng, 0.4 film + 0.4 core)`. The film smooths the surface detail.
* `wetness = max(wetness, film)`. The existing wet response applies (per-class darkening × porosity, roughness scale).
* streams: `roughness = mix(roughness, max(0.5 roughness, 0.06), core)`, `diffuse *= 1 - 0.08 core`.
* F0 is unchanged. No tint, no emission, no white lines.

## Cvars

| cvar | default | |
|---|---|---|
| `r_weatherRunoff` | 0 | on / off (needs `r_weatherWetness`) |
| `r_runoffStrength` | 1 | film strength (0.001 – 2) |
| `r_runoffSpeed` | 24 | pulse speed, world units / s |
| `r_runoffScale` | 48 | streak cell size, world units |
| `r_runoffProbe` | 1.5 | wall exposure probe, weather-map texels (0 off, ≤ 32 units) |
| `r_runoffEntities` | 0 | runoff on characters, props and movers too |

## Debug (`r_weatherWetnessDebug`)

| | |
|---|---|
| 21 | geometric normal |
| 22 | slope class: blue puddle, green slope, yellow steep, red facing down (blended) |
| 23 | projected gravity `F·0.5+0.5`, black where flat |
| 24 | runoff mask (blue film, white cores), red = exposure added by the wall probe; grey off, magenta excluded draw |
| 25 | ungated animated flow field (grey film, cyan streams) |
| 26 | final roughness |
| 4 | dry / wet split (left half without runoff too) |

## Cost

NVIDIA program, `USE_WETNESS;PER_PIXEL_LIGHTING;USE_LIGHT_VECTOR;USE_SPECULARMAP;USE_NORMALMAP;USE_CUBEMAP`:
1353 → 2373 fp instructions in total (static). Almost all of it is inside branches:

* all pixels: about 3 instructions (classification);
* wet runoff pixel, 1 bin: about 210 (setup, bin select) + 285 (streak field) + 97 (wall probe, 5 texel fetches, only
  on steep faces with exposure < 1). That is about 600, or 900 in the bin-blend zones;
* debug views: 108, taken only while debugging.

Off by default. It costs nothing when it is not raining (`u_WetnessParams.x = 0`) or when `r_weatherRunoff` is 0.

## Validation

Offline:
* GLSL compiled on Intel UHD and RTX 2060 (± PARALLAXMAP / SSR / CUBEMAP / SPECGLOSS, vertex lit).
* Python port of the pattern (`scratchpad harness.py`) on 10° / 30° / 60° / 90° planes at 4 yaws: streaks are 3–7×
  longer along `F` than across. The two-axis version failed on diagonal slopes (ratio ≈ 1), which is why there are 8
  bins.
* flow `z ≤ 0` for 100k random normals.
* clock wrap difference 3e-14.
* pulses move down at 9.5 / 5.5 units per 0.2 clock cells (expected 9.6 / 5.6).
* wind lean +10° / +20° for shear 0.2 / 0.35 (expected +11° / +19°).
* MSVC SP + MP build.

**Not seen in game.** Checklist on a rain map: slopes at 10° (puddles, no streaks), 30°, 60°, a vertical wall, a
ceiling (dry), stairs (treads puddle, risers streak), rock, a roof, a wall under a roof (dry) against an exposed
exterior wall (debug 24). Look for upward flow, swimming while moving the camera, seams on corners and bin blends, and
identical parallel stripes across the map.

Optional quality step, not needed: an authored flow or streak-mask texture sampled in the same (across, z) frame
would give sharper drip lines under ledges. The procedural baseline doesn't need it.
