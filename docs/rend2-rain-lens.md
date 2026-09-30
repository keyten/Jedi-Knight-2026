# Rend2 lens rain (`r_rainLens`)

Optional, off by default. Procedural rain drops on the camera lens while it rains
and the camera is outside. Code: `shared/rd-rend2/tr_rainlens.cpp`,
`shared/rd-rend2/glsl/rainlens.glsl`, `rainlens_composite.glsl`.

## Insertion point

`RB_PostProcess` (`tr_backend.cpp`), HDR, after the MSAA resolve:

```
MSAA resolve
[SMAA 2: edges + temporal resolve]   <- history stays drop free
[motion blur]
lens rain                            <- RB_RainLens
dynamic glow / bloom extraction      <- scene extraction sees refracted lights
[SMAA 1 edges]                       <- edges of the refracted image
tone map -> sun rays -> glow composite -> debug overlays -> refraction fill
```

When lens rain runs, `RB_PostProcess` uses the ordering branch that motion blur
already uses. Without lens rain the ordering doesn't change. SSR and SSGI resolve
in the main pass (before post-processing), so they're unaffected.

Modern bloom prefilter refracts its separate emissive MRT with the same lens
field, so the glow follows the displaced source. The first legacy dynamic
glow downsample applies the field too. Refracted scene energy also contributes
when `r_bloomSceneIntensity` (modern) or `r_dynamicGlowBloom` (legacy) is
above zero.

## Targets

`textureScratchImage` is 256×256 RGBA8, so it can't be used here. The effect has
a dedicated target: `tr.rainLensImage` (full resolution, same HDR format as
`renderImage`) plus `tr.rainLensFbo`. A transient `RGBA16F` lens field stores
offset, coverage and blur radius at half screen height (at least 540 pixels,
unless the display is smaller). They're allocated only with `r_rainLens 1`
(latched) and `r_hdr 1`. The field pass evaluates droplet state once per field
pixel; the full resolution pass composites it with `srcFbo->colorImage[0]`.
Modern bloom prefilter samples the same field for emissive refraction.
Subsequent color passes read `rainLensImage` directly; the original resolved
FBO supplies depth for the final depth blit. No pass reads and writes the same
texture.

## Activation

Decided on the front end (`R_AddPostProcessCmd` → `R_RainLensExposure`), because
`R_IsOutside` mutates a cache. All of these must hold:
- `r_rainLens` is on and the target exists;
- it's a world scene (`!RDF_NOWORLDMODEL`);
- the view is `VPT_MAIN`, not a portal, mirror or depth-shadow view;
- `weatherSlots[WEATHER_RAIN].active`;
- `R_IsOutside(vieworg)`.

Intensity = `particleCount / 5000`, clamped to 0.25..1.

Portals, mirrors, cubemap captures and shadow views never reach `RB_PostProcess`
anyway. On the back end, only the first post-processed world scene of a frame
owns the lens.

## State

In the default procedural mode, backend state is a lens clock (game time, so
it pauses with the game), the last exposed time, and a 1.5 s ramp-up that
resets once the lens has drained under cover. A cut clears it.

`r_rainLensSimulation 1` selects persistent water. A 256-cells-high lens
lattice stores mass, velocity and wetness, updated at 30 Hz in game time.
Impacts add water; mass and momentum flow conservatively to neighbors and
merge there. Small deposits pin to the surface, while wet paths lower the
pinning threshold and retain thin trails. Gravity is projected from world
down onto the camera's right/up axes, so roll changes flow direction and
looking vertically reduces it. No impacts are added under cover; mass and
wetness decay there. Cuts clear the lattice. Its RGBA16F texture is uploaded
only when a simulation step runs. The lens field is regenerated only on those
uploads; the HDR optics composite still runs every displayed frame. The
procedural mode remains the default and needs no state upload.

## Droplet model

Everything lives in lens space, normalised by the screen **height**, so the drop
size holds across resolutions, ultrawide and FOV changes.
- **Beads:** a hashed 13-cells-per-height grid. Each cell has a life cycle of
  4–10 s (grow, sit, evaporate/shrink). There's a density test per cycle.
- **Sliders:** 4.5 columns per height, one drop per column per 14–18 s cycle. A
  drop sticks for 0.8–3.5 s, then slides with stick-slip motion
  `travel = v(s − 0.9·sin(2πns)/(2πn))`. It gets a tail stretched by its speed,
  a thin trail anchored to the drop's path that dries according to approximate
  time since passage and leaves stationary small beads behind, and it
  wipes the beads it crosses. At partial overlaps, normals are blended to
  soften the refraction transition; water mass is not simulated in this mode.
- **Shape:** a spherical cap `h = sqrt(1 − r²)`. It's egg-shaped
  (`r·(1 + q·lopsided)`) with a small hashed ellipse, and the edge is softened
  with `1 - smoothstep(0.78, 1, r)`. The slope includes the derivatives of the
  egg shape, ellipse, and stretched tail.
- **Refraction:** `offset = −slope · radius · r_rainLensRefraction · 1.6 · mask`.
  This produces a magnified, inverted image. Aspect is corrected.
- **Optics:** a 5-tap disc blur inside the drop only, radius ∝ drop size. The
  rim is darkened artistically (no reflected environment is available). The glint is
  proportional to the local refracted luminance. There are no white spots and no
  global darkening.
- **Under cover:** drops born after the last exposed time are never shown.
  Existing drops fade over 1.2 s and sliders run off faster. The pass is skipped
  after that, so it costs nothing indoors.

## Cvars

| cvar | default | |
|---|---|---|
| `r_rainLens` | 0 | latched, allocates the target, needs `r_hdr` |
| `r_rainLensSimulation` | 0 | latched, persistent water mode; requires `r_rainLens 1` and `vid_restart` |
| `r_rainLensDensity` | 0.5 | density |
| `r_rainLensRefraction` | 1.0 | |
| `r_rainLensDropSize` | 1.0 | drop size |
| `r_rainLensDebug` | 0 | cheat. Forces the effect on everywhere. 1 = coverage (red) / blur radius (green) in simulation mode, mask / trail film in procedural mode; 2 = normal, 3 = UV offset ×40, 4 = scene / composition split |

GPU time: `r_speeds 100` shows the "Rain lens" timed block.

In procedural mode the field is regenerated every frame. Its sliders follow
screen-down gravity and it does not retain mass. The simulation mode keeps
water state and projects gravity, but is still a small screen-space lattice,
not a full fluid solver or optical model of a real camera objective.

## Optional asset

None is required. A normal/height atlas of real drop shapes could replace
`Cap()` for more irregular silhouettes. It would ship in an optional pk3, and
isn't part of the baseline.

## Validation checklist (in game, not yet done)

- In procedural mode, heavy and light rain; walking under a roof (no new drops,
  drain in about 1 s) and back out (1.5 s ramp).
- With `r_rainLensSimulation 1`, inspect merging, wet trails, cover transitions,
  camera roll and vertical views. Verify that cuts clear the lens and paused
  game time freezes the simulation.
- With `r_bloom 1`, compare the saber/glow position inside and outside drops;
  check bright lights and rapid turns (drops stay fixed on the screen).
- FOV changes, resolutions, ultrawide, and `r_rainLensDropSize 0.25` (small
  beads must survive field sampling).
- MSAA, `r_smaa 1`, `r_smaa 2` (no drop ghosting), `r_motionBlur`, `r_ssr`.
- Tone mapping and auto exposure.
- No effect in mirrors, portals, cubemap bakes (`r_cubemapping` rebuild) or UI
  3D models.
- Timings at 1080p and 1440p/4K.
