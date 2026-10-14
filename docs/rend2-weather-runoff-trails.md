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

## Visible runoff drips: deliberately not planned

Visible drip particles have **not been implemented and are not on the current
roadmap**. This is a deliberate scope decision rather than unfinished Phase 1
work: they are expensive to make temporally robust and are rarely noticeable
outside close-up captures.

For the current world-weather presentation, the expected improvement is
modest: small beads and short strings would only be clearly visible at close
range, while procedural runoff plus persistent material trails already provide
most of the readable wet-surface response. A robust screen-space simulation
would add substantially more state, synchronization, failure handling and
testing for that comparatively small visual gain. An unstable approximation
would be worse than no particles: drops would float, jump between objects,
survive camera cuts or detach from the wrong silhouette.

They should therefore be reconsidered only if close-up visual captures show
that discrete drops are a material improvement over Phase 1. It must remain an
optional, default-off layer and must not hold the world trail implementation
hostage. No inactive drip/rate/lifetime cvars are registered yet; they should
be added together with a working prototype rather than becoming no-op public
controls.

## What Phase 2 means in simple terms

Current runoff is an animated material pattern. There are no individual drops
with positions, velocities or lifetimes. Phase 2 would add small GPU records
that behave like drops which understand the visible surface:

1. Spawn a drop on a wet visible pixel.
2. Reconstruct the corresponding 3D surface point from scene depth.
3. Read its geometric normal and project gravity onto the surface.
4. Advance the drop downhill, project it back to the screen and attach it to
   the new depth surface.
5. Reject the update if the geometry is missing, changed or implausibly far
   away.
6. Deposit into Phase 1 trail memory while the drop remains attached.
7. At a strong depth/normal discontinuity, detach it into an ordinary falling
   droplet and optionally emit a splash on the next collision.

In shorthand:

```
screen position -> reconstruct surface -> move along projected gravity
                -> reproject and validate -> trail or detach
```

The result would be small reflective beads and short streams that follow walls
and slopes, leave a drying path, fall from ledges and sometimes create a small
splash. The main qualitative improvement is causality: the player can see one
drop travel, leave a mark and fall, instead of only seeing a moving water
pattern. It is detail rather than a major change to the weather silhouette.

## What the deeper Naughty Dog system did

Naughty Dog's SIGGRAPH 2020 system was not merely a depth collision added to a
sprite pool. In plain terms, its parts served these purposes:

| Mechanism | Practical meaning |
|---|---|
| Compute particles | The GPU owns and updates a large array of drop state without a CPU call per drop. |
| Depth reconstruction | A depth pixel is converted back into the 3D point the camera sees. |
| Screen-space normals | Each visible pixel tells the drop which way its surface faces. |
| Projected gravity | The component pointing into the surface is removed, leaving downhill motion along it. |
| Motion vectors | A drop predicts where the piece of moving character geometry went this frame. |
| Object IDs | The update verifies that the new pixel still belongs to the same object. |
| Reprojection | Last frame's particle is located and corrected against this frame's buffers. |
| Ribbons | Ordered particle points are connected into a thin stream instead of unrelated round beads. |
| Curvature/edge tests | Neighboring depth and normals reveal a ledge or sharp bend where water should detach. |
| UV-space transfer | A temporary screen-space drip becomes a permanent mark in a character texture. |

Its compute framework supported spawn/update/kill jobs, event buffers,
particle-to-particle ribbon communication and invalid ribbon members. Surface
attachment reconstructed world position from depth, predicted the next frame
with motion vectors, then corrected that prediction. Permanent character marks
were transferred from screen space into object UV space using object IDs,
triangle lookup and barycentric coordinates. That last step lets a stain remain
on animated skin or clothing after it leaves the screen and returns later.

Copying that complete character system would be inappropriate for the current
world-first goal.

## Rend2 gaps and minimum world prototype

Rend2 has some useful inputs, but not the complete simulation contract:

- scene depth and screen-space normal/material outputs exist for rendering;
- weather rain uses transform feedback with a specialized position/velocity
  and optional impact record;
- ordinary FX sprites are CPU-authored/rendered, while volumetric particles are
  renderer inputs rather than a general GPU surface simulation;
- there is no shared object-ID plus motion-vector attachment path for surface
  ribbons;
- there is no common camera-cut/disocclusion invalidation contract for them.

A reduced static-world prototype would still require:

- a GPU particle pool containing position, previous position, velocity, age,
  lifetime, attachment state and validity;
- a compute or transform-feedback update pass;
- depth/normal reconstruction and projected-gravity movement;
- conservative reprojection and surface-distance validation;
- edge/curvature detection and a detachment event buffer;
- camera-cut, teleport, map-change and long-frame resets;
- analytic or existing soft-particle rendering, depth tested against the scene;
- injection into the persistent world trail field;
- capacity limits, LOD, debug views and real-map temporal validation.

Ribbons add another layer: points in one stream must remain ordered, find their
neighbors, lose invalid members safely and render without gaps. Moving entities
add motion vectors, stable object IDs and attachment correction. Characters add
triangle/barycentric lookup, UV transfer and per-character persistent wetness
targets. These are later projects, not prerequisites for a world prototype.

If implemented, the controls must be separate and default off:

- persistent trail lifetime/strength;
- visible drip enable;
- drip spawn rate and capacity;
- drip lifetime;
- surface-follow/detachment thresholds;
- debug visualization.

## Cost and risk

A rough engineering scale for an experienced graphics programmer is:

| Scope | Relative effort |
|---|---|
| Static-world proof of concept with point drops | Medium; approximately 1-2 weeks |
| Stable world drips with cuts, edges, splashes, budgets and debug | Medium-high; approximately 3-6 weeks total |
| Production ribbons, LOD and broad real-map validation | High; several additional weeks |
| Moving entities and persistent character drips | Very high; a separate multi-month project |

These are scope indicators, not delivery estimates. Projected gravity itself is
simple; most of the work is preventing temporal errors at disocclusions,
silhouettes, thin geometry, camera cuts and rapidly changing visibility.

The screen-space approach also remains inherently view dependent. Drops cannot
be simulated reliably on hidden surfaces, at the edge of the screen or through
long occlusions without another representation. A world-only prototype should
kill or reset uncertain particles rather than pretending they survived.

## Validation

Check a rain-exposed sloped wall with debug mode `2`, stop rain, and verify the
cyan stream channels fade without moving. Covered walls remain zero because
the ordinary roof/wall-probe exposure tests still gate retained activity.
Loading a map, restarting the renderer, a backwards clock or a long time jump
clears the state with the rest of persistent wetness.

