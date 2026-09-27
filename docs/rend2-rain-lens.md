# Rend2 lens rain (`r_rainLens`)

Optional, off by default. Procedural rain drops on the camera lens while it rains
and the camera is outside. Code: `shared/rd-rend2/tr_rainlens.cpp`,
`shared/rd-rend2/glsl/rainlens.glsl`.

## Insertion point

`RB_PostProcess` (`tr_backend.cpp`), HDR, after the MSAA resolve:

```
MSAA resolve
[SMAA 2: edges + temporal resolve]   <- history stays drop free
[motion blur]
lens rain                            <- RB_RainLens
dynamic glow / bloom extraction      <- refracted lights bloom where they are seen
[SMAA 1 edges]                       <- edges of the refracted image
tone map -> sun rays -> glow composite -> debug overlays -> refraction fill
```

When lens rain runs, `RB_PostProcess` uses the ordering branch that motion blur
already uses. Without lens rain the ordering doesn't change. SSR and SSGI resolve
in the main pass (before post-processing), so they're unaffected.

## Targets

`textureScratchImage` is 256×256 RGBA8, so it can't be used here. The effect has
a dedicated target: `tr.rainLensImage` (full resolution, same HDR format as
`renderImage`) plus `tr.rainLensFbo`. They're allocated only with `r_rainLens 1`
(latched) and `r_hdr 1`. The pass reads `srcFbo->colorImage[0]`, writes
`rainLensFbo`, and blits colour attachment 0 back (the motion blur pattern). It
never reads and writes the same texture.

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

Backend state is a lens clock (game time, so it pauses with the game), the last
time the camera was exposed, and a 1.5 s ramp-up. A cut (map change, or a time
jump over 1 s) clears it. There is no global rain transition.

## Droplet model

Everything lives in lens space, normalised by the screen **height**, so the drop
size holds across resolutions, ultrawide and FOV changes.
- **Beads:** a hashed 13-cells-per-height grid. Each cell has a life cycle of
  4–10 s (grow, sit, evaporate/shrink). There's a density test per cycle.
- **Sliders:** 4.5 columns per height, one drop per column per 7–13 s cycle. A
  drop sticks for 0.8–3.5 s, then slides with stick-slip motion
  `travel = v(s − 0.9·sin(2πns)/(2πn))`. It gets a tail stretched by its speed,
  a thin trail that dries from the top and leaves small beads behind, and it
  wipes the beads it crosses.
- **Shape:** a spherical cap `h = sqrt(1 − r²)`. It's egg-shaped
  (`r·(1 + q·lopsided)`) with a small hashed ellipse, and the edge is softened
  with `smoothstep(1, 0.78, r)`. Slope = `q / max(h, 0.35)`.
- **Refraction:** `offset = −slope · radius · r_rainLensRefraction · 1.6 · mask`.
  This produces a magnified, inverted image. Aspect is corrected.
- **Optics:** a 5-tap disc blur inside the drop only, radius ∝ drop size. The
  rim is up to 9% darker (Fresnel) and transmission is 0.97. The glint is
  proportional to the local refracted luminance. There are no white spots and no
  global darkening.
- **Under cover:** drops born after the last exposed time are never shown.
  Existing drops fade over 1.2 s and sliders run off faster. The pass is skipped
  after that, so it costs nothing indoors.

## Cvars

| cvar | default | |
|---|---|---|
| `r_rainLens` | 0 | latched, allocates the target, needs `r_hdr` |
| `r_rainLensAmount` | 0.5 | density |
| `r_rainLensRefraction` | 1.0 | |
| `r_rainLensScale` | 1.0 | drop size |
| `r_rainLensDebug` | 0 | cheat. Forces the effect on everywhere. 1 = mask (r) / trail film (g), 2 = normal, 3 = UV offset ×40, 4 = scene / composition split |

GPU time: `r_speeds 100` shows the "Rain lens" timed block.

## Optional asset

None is required. A normal/height atlas of real drop shapes could replace
`Cap()` for more irregular silhouettes. It would ship in an optional pk3, and
isn't part of the baseline.

## Validation checklist (in game, not yet done)

- Heavy and light rain; walking under a roof (no new drops, drain in about 1 s)
  and back out (1.5 s ramp).
- Bright lights and sabers blooming through drops; rapid turns (drops stay fixed
  on the screen).
- FOV changes, resolutions, ultrawide.
- MSAA, `r_smaa 1`, `r_smaa 2` (no drop ghosting), `r_motionBlur`, `r_ssr`.
- Tone mapping and auto exposure.
- No effect in mirrors, portals, cubemap bakes (`r_cubemapping` rebuild) or UI
  3D models.
- Timings at 1080p and 1440p/4K.
