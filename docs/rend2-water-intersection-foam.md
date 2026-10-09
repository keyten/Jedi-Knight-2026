# Object/intersection foam

`r_waterIntersectionFoam 1` lets opaque geometry create foam where it crosses
a modern water surface. It uses the modern water pass and its opaque scene
depth; it does not enable or reuse an ocean renderer, require a material tag,
or add an asset.

## Detection

For each visible water fragment the shader projects a small world-space ring
around the water position into the copied opaque depth. A sample contributes
only when all of the following hold:

- its reconstructed world position is inside a narrow slab around the water
  plane;
- neighbouring depth samples form a coherent opaque surface;
- the reconstructed opaque normal crosses the water plane rather than running
  parallel to it.

The last two tests reject ordinary depth silhouettes and shallow floors, so
the feature does not draw a white outline around every discontinuity. Sky and
samples outside the active view are rejected. BSP rocks, props and animated
characters follow the same test and need no foam tag.

## Persistence and flow

When `r_waterInteraction 1` has allocated a body field, the copied depth is
also projected into that body's existing XY grid once per main view. Qualified
contacts feed a separate intersection tracer. It has its own decay, but uses
the interaction solver's mask, diffusion and resolved flow velocity. At upload
it is combined with splash foam in the existing B channel; no second GPU foam
atlas is allocated.

The CPU depth projection is deliberately conditional. With the interaction
field disabled (or persistence set to zero), there is no depth readback and
only the immediate shader result is used. Portal, mirror and reflection views
never inject the field.

`FoamMultiplier` in a water body's `env.json` rule scales both immediate and
persistent intersection foam; zero disables it for that body.

## Controls

| CVar | Default | Meaning |
|---|---:|---|
| `r_waterIntersectionFoam` | 0 | Master switch |
| `r_waterIntersectionFoamWidth` | 8 | Intersection half-width in world units |
| `r_waterIntersectionFoamStrength` | 1 | Source and immediate display strength |
| `r_waterIntersectionFoamPersistence` | 1.5 | Persistent tracer e-folding lifetime in seconds; 0 selects immediate-only |
| `r_waterIntersectionFoamDebug` | 0 | Show the raw qualified mask (orange) |

For persistence, enable `r_waterInteraction 1` and restart the renderer because
the interaction atlas is a latched resource. `r_waterFlow 1` enables advection
using the body's already resolved flow from the directional-flow system.

## Validation

Use a shallow stream containing a BSP rock and a character. First enable only
the master switch and verify the immediate contact follows both silhouettes.
Then enable the interaction field, restart, and verify foam remains briefly
after the character leaves and travels downstream when body flow is enabled.
With the debug mask active, submerged floors, airborne objects and unrelated
depth edges should remain black.
