# Persistent runoff trails

## Scope

Phase 1 keeps `lightall` runoff procedural and geometry-aware, but lets its
world-anchored stream channels leave a dark, smooth trail that dries after the
water passes. It is static-world only. Characters, props, movers and the view
weapon do not read world trail history.

Enable the existing wetness/runoff stack and choose a non-zero lifetime:

```
r_weatherWetness 1
r_weatherWetnessPersistence 1
r_weatherRunoff 1
r_weatherRunoffTrailPersistence 20
```

`r_weatherRunoffTrailPersistence` is the linear dry time in seconds and
defaults to `0` (off). `r_weatherRunoffTrailDebug` also defaults to `0`; mode
`1` shows the retained runoff envelope and mode `2` shows the final
geometry/slope/exposure-gated trail mask.

## State and shading

No render target or texture binding was added. The existing persistent wetness
`RGBA32F` ping-pong now stores:

- `R/G`: retained flat/steep water;
- `B`: copied weather depth for roof/exposure tests;
- `A`: recent world runoff activity.

The fixed-rate wetness pass deposits the current rain intensity into `A` while
runoff is enabled and dries it over the configured lifetime. `lightall` samples
that envelope only for static world draws. The actual trail remains
world-anchored geometry: the same contour-aligned runoff noise returns both the
animated water and a static stream-channel mask. Geometric slope, projected
gravity, rain exposure, the wall probe, windward response, material runoff
scale and the ordinary wetness exclusions still gate the result.

This split avoids a false screen-space history: camera motion and cuts cannot
move or resurrect a trail, because the temporal state lives in weather/world
projection and the visible mask is reconstructed from world position and the
surface geometric normal.

## Why visible drips remain Phase 2

Naughty Dog's SIGGRAPH 2020 system was not merely a depth collision added to a
sprite pool. Its compute framework supported spawn/update/kill jobs, event
buffers, particle-to-particle ribbon communication and invalid members. Surface
attachment reconstructed world position from depth, predicted the next frame
with motion vectors, then corrected the attachment. Permanent character marks
were transferred from screen space into object UV space using object IDs,
triangle lookup and barycentrics.

Rend2 currently has useful ingredients but not that contract:

- scene depth and screen-space normal/material outputs exist for rendering;
- weather rain uses transform feedback, with a specialized position/velocity
  (and optional impact) record;
- ordinary FX sprites are CPU-authored/rendered, while volumetric particles are
  renderer inputs rather than a general GPU surface simulation;
- there is no shared object-ID plus motion-vector attachment path for a surface
  ribbon, nor a camera-cut invalidation contract for it.

Consequently Phase 2 should be a separate prototype, default off, with its own
visible-drip enable, spawn rate, lifetime and debug cvars. Before shipping it
must reproject through depth and normal, reject disocclusions/object-ID changes,
reset on camera cuts and long frame gaps, and emit an ordinary droplet/splash
event when projected gravity reaches a strong curvature/depth edge. It must not
reuse the world trail texture as particle position history or block Phase 1.

## Validation

Check a rain-exposed sloped wall with debug mode `2`, stop rain, and verify the
cyan stream channels fade without moving. Covered walls remain zero because
the ordinary roof/wall-probe exposure tests still gate retained activity.
Loading a map, restarting the renderer, a backwards clock or a long time jump
clears the state with the rest of persistent wetness.

