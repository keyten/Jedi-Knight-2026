# Waterfall impact-pool turbulence

This optional path makes only the receiving water below a waterfall turbulent.
It reuses the existing body-local interaction solver and persistent foam field;
it does not modify BSP data and does not add a general fluid solver.

## Source resolution

Every classified waterfall sheet resolves an impact origin from:

1. a matching `WaterfallEmitters` entry with an explicit `Origin`;
2. for `ExistingFx: true`, the nearest exact `env/waterfall_mist` `fx_runner`
   origin in the map entity string;
3. otherwise, the classified sheet bottom/impact edge.

The origin must resolve onto a nearby horizontal modern-water surface. The
renderer tests the actual surface triangles, not the containing liquid brush,
and clips an edge hit to the nearest surface point. If no reliable receiving
surface is found, that source stays inactive. Add a map-specific
`WaterfallEmitters` entry with `Origin` and, optionally, `Radius` in
`cubemaps/<map>/waterfalls.json`.

## Response

At each fixed interaction step the source applies a compact downward velocity
kick at its center. Deterministic, irregular secondary kicks around the center
break symmetry. The ordinary local ripple field supplies the propagated waves.
The persistent foam source uses the waterfall channel, which creates larger
local normals, increased effective roughness, and dense foam with smooth field
falloff. Where a pool has no authored flow, the foam update approximates
outward transport from the gradient of the local source footprint.

Waterfall mist remains controlled independently by `r_waterfallMist`; impact
strength shares the resolved waterfall profile's `ImpactStrength` but does not
enable mist.

## Controls

All impact behavior is off by default.

| CVar | Default | Meaning |
|---|---:|---|
| `r_waterfallImpact` | `0` | master switch |
| `r_waterfallImpactRadius` | `1` | authored/inferred radius multiplier |
| `r_waterfallImpactImpulse` | `1` | central downward disturbance strength |
| `r_waterfallImpactTurbulence` | `1` | stochastic secondary disturbance and local whitewater-normal strength |
| `r_waterfallImpactFoam` | `1` | persistent foam injection strength |
| `r_waterfallImpactDebug` | `0` | draw impact circles/axes and print resolution diagnostics |

`r_waterInteraction 1` (latched) enables physical ripples. Preferably also use
`r_waterFoamField 1` (latched) for persistent foam and the full local surface
response. A cyan debug circle is an inferred resolved source, amber is an
authored/existing-FX origin, and red is unresolved. `r_waterfalls` lists the
sheet-to-pool mapping and reports when an explicit emitter is required.
