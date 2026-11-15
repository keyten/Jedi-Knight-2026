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

At each fixed interaction step the source applies a small downward acceleration
to a compact core. Uneven packets of water periodically raise a crest outside
that core, distributed across the foot of the sheet. Their footprints cover
several interaction texels. The ordinary body solver carries the outgoing waves
downstream and reflects them at its masked banks.
The persistent foam field receives a broad, broken aeration footprint oriented
across the sheet and extended downstream. Its width includes the sheet width
and lateral spread; its length and density grow with the drop to the resolved
receiving surface. Drop energy is a bounded square-root proxy, since stock BSP
sheets do not supply discharge. Adjacent tall spillway strips therefore form
an overlapping foam apron instead of isolated short smears. The existing flow and decay
carry the foam away from the contact. Where a pool has no authored flow, the
foam update approximates outward transport from the gradient of the source.

The irregular lower regime of the falling sheet converges toward the same
lit foam colour as the receiving water. The dense pool source covers the
join with broad, changing patches instead of a separate straight rim.

The existing 128-texel foam field carries the broad footprint. From 700 to
1800 world units the surface shader averages bubble detail into macro coverage.
The pattern textures retain their automatic mip filtering. Tall sheets seen
from over 768 units above their foot use the low-detail regime for the whole
draw at quality 0/1, so a nearby lip cannot keep the far-below foot detailed.
Quality 2 skips this additional cap. No extra draw pass or particles are added; existing droplet
distance limits still apply. This is the main viewing regime on `t3_hevil`.

With `r_waterfallMist 1`, resolved contacts also emit short ballistic droplets.
They use the baked light grid, share one alpha-blended material, and are
distance-limited to at most 64 visible quads per scene. The droplets do not
require the volumetric particle system.

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
