# Directional water flow

Directional flow is a cheap, per-water-body velocity used to advect medium and
micro surface detail. It does not move the water volume or translate vertices.
It is disabled by default with `r_waterFlow 0`; that path retains the exact
pre-flow modern-water inputs, legacy `tcMod`s and ambient-wave behavior.

## Installed stock audit and automatic decisions

`tools/rend2/water_audit.py` reads the installed PK3s without modifying them.
The complete machine-readable result is
[`rend2-water-body-stock-audit.json`](rend2-water-body-stock-audit.json). It
contains, for every candidate surface and body:

- map/body/surface/brush membership, bounds, contents, orientation and liquid
  relationship;
- the winning shader file and PK3, every stage directive/map/blend, every
  scroll/turb/stretch, and every vertex deform;
- the base-UV world tangents, mapping coverage and mirror sign for every
  surface;
- each scroll's converted world velocity and within-body basis agreement;
- cross-stage agreement, confidence, resolved flow source/direction/speed and
  the reason an automatic flow was accepted, suppressed, or rejected.

The conversion treats a positive texture offset as motion of a fixed texture
feature in the negative UV direction:

`worldVelocity = -scroll.x * dp/du - scroll.y * dp/dv`

The tangents come from actual BSP triangle positions and base texture
coordinates. Area-weighted agreement is checked both across patches of a body
and across stages. A stage is usable only with at least 80% valid mapping and
80% directional agreement; all usable stages must agree by at least 72% for a
high-confidence automatic result. This handles rotated, mirrored and sloped
mappings without treating UV as world XY. Time-varying turbulence and stretch
remain diagnostics. A stage containing an additional scale, transform,
rotation or entity translation is recorded but conservatively rejected for
automatic flow until its complete ordered transform can be inverted; an
explicit override is required instead of guessing. No used stock water stage
contains one of those unsupported transforms.

The 50 installed stock water bodies resolve as follows:

| Maps / bodies | Result |
| --- | --- |
| `t3_hevil` 1-8 and 10-11 | high-confidence legacy-derived motion (1-8 are legacy-rendered waterfalls; 10-11 are modern horizontal bodies) |
| `yavin1` 1-5 | high-confidence legacy-derived, separately converted per body |
| `yavin1b` 1-7 | high-confidence legacy-derived, separately converted per body |
| `t2_port` 2-10, `taspir2` 9-12 | ambiguous: the two `bespin/water2` stages oppose each other; no automatic flow |
| `yavin2` 1-2 | ambiguous: stages agree in UV, but the curved/multi-patch body bases agree only 72-73%; no uniform flow is guessed |
| `t3_bounty` 1-2 | authored direction is high confidence but suppressed by `still_pool` |
| `t3_hevil` 9 | authored direction is high confidence but suppressed by the explicit `lake` profile |
| `t2_rancor` 1-6, `t2_trip` 1-2, `vjun2` 1, `t2_port` 1 | no usable scroll direction; no flow |

This is 22 legacy-derived flows, 15 ambiguous/no-flow decisions, 10 bodies
without a scroll direction, and 3 high-confidence directions deliberately
suppressed by still/lake profiles. Slime and lava do not gain flow. The same
shader resolves differently on separate bodies because conversion is per body.
No stock body has a manually authored flow override.

## Resolution and authoring

Flow state is stored per physical body as world direction, speed, velocity,
confidence, and source (`explicit`, `legacy-derived`, `profile default`, or
`none`). Rules use the existing per-map `cubemaps/<map>/env.json`
`WaterBodies` array. Exact shader, shader prefix, body ID, point/origin and
bounds selectors are supported and may be combined. Prefer point/bounds over a
map-local diagnostic body ID.

```json
{
  "WaterBodies": [{
    "Selector": { "Shader": "textures/example/stream", "Point": [128, 256, -64] },
    "DynamicsProfile": "slow_stream",
    "Flow": { "Direction": [1, 0.2, 0], "Speed": 18 }
  }]
}
```

Omitting `Speed` uses the profile default (32 world units/s times its flow
value) and reports source `profile default`. The earlier two-component
`"Flow": [x, y]` form remains an explicit world velocity; `FlowSpeed` can
override its magnitude. A zero speed is an explicit no-flow decision.

Development-only rules do not edit the BSP or env file:

```
r_waterFlowOverride body <id> <x> <y> <z> <speed>
r_waterFlowOverride shader <exact-name> <x> <y> <z> <speed>
r_waterFlowOverride prefix <name-prefix> <x> <y> <z> <speed>
r_waterFlowOverride list
r_waterFlowOverride dump
r_waterFlowOverride clear
```

Later rules win. `clear` removes runtime rules only and re-resolves saved env
rules. `r_waterBodies` and `r_waterBodies dump` include legacy vectors, UV
bases, agreement/confidence and final decisions.

## Rendering and consumers

The resolved velocity advects the two existing world-space procedural slope
scales at different speed multipliers and small opposing angular biases. The
medium analytical wave band receives smaller phase advection; macro waves keep
their profile propagation. The third detail layer retains the winning legacy
stage's live `tcMod`. Optional geometric displacement uses the advected phase
but remains bounded around the base surface—vertices never translate
downstream.

There is no interactive body-space disturbance solver, persistent foam, or
object-wake solver in the current code. Flow is exposed without inventing
parallel systems:

- `R_WaterFlowAtWorldPosition(position, velocity, bodyId)`;
- `R_WaterFlowForBody(bodyId, simulationCoordinate, velocity)`.

The second API currently returns uniform flow and deliberately accepts a
body-space coordinate. A future disturbance, foam or wake field can consume it
without changing the representation. The curved `yavin2` bodies show why an
optional spatial form is needed: the planned lightweight extension is a short
body-space polyline/spline of direction-speed control points, baked at map load
to a small two-channel body-local texture. Uniform bodies keep zero texture
memory and zero extra samples. No general fluid solver or mandatory flow map
is warranted by the stock set.

## Controls and diagnostics

- `r_waterFlow 0|1`: master control, default `0`.
- `r_waterFlowSpeed 0..4`: global speed multiplier, default `1`.
- `r_waterFlowDetail 0..4`: medium/micro advection scale, default `1`.
- `r_waterFlowDebug 0..4`: `1` direction, `2` magnitude, `3`
  confidence/source, `4` advected detail. Its world overlay draws resolved or
  legacy arrows; mode 3 draws red U and green V tangent bases.
- `r_waterBodies draw`: bounds, representative plane, ID/profile and arrows.

## Cost and limits

Resolution runs once at map load. It adds tangent accumulation over already
visited liquid triangles plus comparisons among a body's few scroll stages.
Persistent state is a handful of vectors/scalars per surface, body and scroll.
Rendering adds no texture, target, sampler, permutation or draw call; enabled
flow adds two small 2D rotations/subtractions to existing micro samples and one
subtraction for each selected medium analytical term.

On the available Intel UHD Graphics GL 3.2 system, an interleaved synthetic
1920x1080 full-screen quality-1 run measured 19.018 ms with resolved flow off
and 19.443 ms on: +0.424 ms. This is a deliberately worst-case full-screen
water workload; normal stream coverage is much smaller. Stock `yavin1` body
construction, including flow analysis, reported 2 ms and approximately 3.7
KiB of body metadata; `yavin2` reported 3 ms and approximately 1.2 KiB.

The baseline is one uniform vector per body. Curved streams whose surface bases
do not agree are reported ambiguous and require explicit/spatial authoring.
Stage opacity is not reliably recoverable as physical importance, so stages
are reported equal/unknown rather than assigned invented weights. Flow does
not yet advect interactions because there is no current interactive-water
solver. No overlay PK3 is required; renderer metadata and existing env
configuration suffice, and no stock archive was changed.
