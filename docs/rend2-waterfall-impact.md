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

Neighbouring heavy falls with drops over 1024 units use individual narrow
jets. Their Gaussian cross-sections widen downstream, neighbouring jets merge
by saturating addition, and the outside jets splay away from the group centre.
Their long tails fade smoothly into ordinary water. Shared advected value
noise replaces periodic sine motifs for these spillways. Disconnected groups
stay independent. Isolated medium falls, including both `yavin1b` falls,
retain their previous source footprint, LOD and foam material settings.

The irregular lower regime of the falling sheet converges toward the same
lit foam colour as the receiving water. Spillway sheets use the resolved pool
plane for the lower 192 world units of that blend, with a moving uneven boundary.
The sheet and pool share neutral diffuse light derived from the falls' baked
irradiance. The baked ambient/direct ratio separates the background light from
the direct component; current sun visibility shadows the latter instead of
adding a shadowed sun term on top of an unshadowed baked total. Where the
stock BSP has a lightmap, spillway froth reads that spatial lighting instead
of the body average. Primary lightmap UVs are kept in an existing unused
packed-vertex slot and interpolated by both CPU refinement and tessellation.
This preserves the platform shadow on `t3_hevil`, which supplies no realtime
sun input in the tested preset. The ambient probe supplies a small indirect
floor, and available realtime sun shadows can still occlude the direct part. Fresh dense
foam has the column's white albedo; thinning downstream foam is slightly duller.
The existing heavy sheet turns smoothly along the outflow in its lower part,
ending just above the pool instead of intersecting it at a hard angle. This
uses the existing render mesh and tessellation; current and previous motion
positions use the same static turnover, and patch culling includes its extent.
The same world-space pattern and diffuse mottle continue across that contact.
Only its narrow lower band takes two additional pattern samples; the pool
reuses its existing samples. The dense source covers the join instead of a
separate straight rim. These material adjustments are gated by a resolved,
active spillway foam pool. The packed `u_Water[60]` layout and interaction
source data remain unchanged; `u_WaterSpillway[3]` carries the contact plane,
shared advection, unshadowed lighting, its ambient fraction and the pool UV mapping explicitly.

The existing 128-texel foam field carries only the broad footprint. Ordinary
water averages bubble detail into macro coverage between 700 and 1800 world
units. Spillway pools retain 80% of the filtered pattern at that distance, with
sharper bubble thresholds, larger resolved clusters and a rotated second
pattern sample. Dense boil closes the membrane holes and uses patch/fleck
variation for its diffuse colour; the thinning tails keep bubble lace. This adds pool detail without increasing the field resolution
or the foam-pattern texture-read count. Baked lighting adds one read of the
already loaded stock lightmap. A 256-square RGBA16F receiving-light cache
(0.5 MiB per spillway pool) is projected once at map load. Both the pool and
the turning sheet sample it in world coordinates, so a vertical lightmap
cannot leave a bright rectangle on the receiving shadow. No per-frame pass
is added. The end of the sheet dissolves with the same filtered, advected foam
pattern as the pool, only within 64 world units of the receiving plane after
the turnover. It is based on that physical height, so it cannot open a gap
high above the pool on a long sheet. The terminal discard follows the texture branches, which
also avoids the observed older Intel driver failure with a nested discard.
An indirect floor at 12% of the receiving pool's peak baked irradiance retains
visible froth inside the platform shadow; this approximates unresolved diffuse
bounce and scattering, without adding a lighting pass.
The pattern textures retain their automatic mip filtering. At quality 1 tall
heavy sheets fade their upper-view LOD smoothly from 0.68 toward 0.54, retaining
filtered filaments rather than switching to the coarse cellular regime.
Their refraction fades independently, reaching zero in high upper views while
the filtered strand pattern remains. Quality 0 remains the explicit
low-detail setting and quality 2 skips this additional cap. No extra draw pass or particles are added; existing droplet
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

`r_waterSurfaceDebug 12` shows current sun visibility, or the baked lighting
when a spillway has no realtime sun: dark regions are shadowed. Red means
that neither lighting input is available for this surface. `r_waterfalls` also reports the baked foam ambient fraction.
