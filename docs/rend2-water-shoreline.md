# Displaced-water shoreline and contact

`r_waterShoreline 1` closes the visual contract between the render-only water
mesh and static world geometry. It requires `r_waterSurface 1` and
`r_waterGeometry 1`; all three are latched. Gameplay contents, collision and
the BSP are unchanged.

## Geometry inspected and preprocessing

The loader uses the resolved `waterBody_t`, its linked liquid brushes, base
surface plane, actual triangulated interface surfaces and per-body dynamics
profile. For each body it counts quantized source-triangle edges. Edges used by
one triangle are body-boundary candidates; shared edges are interior. A nearby
opaque, near-vertical world surface crossing the water plane classifies a
candidate as a rigid wall. `still_pool` is a conservative fallback for split or
non-rendered pool walls. Other edges are natural banks.

Each water vertex receives the 2D world distance and nearest boundary class in
the otherwise unused second component of the render-mesh secondary UV. The
value is interpolated through the existing dyadic subdivision. This adds no
boundary texture, sampler or per-frame geometry search. Source boundary edges
are split to at most 64 world units and retained as compact contact segments.

Stable local bathymetry is not present in stock BSP water: linked brush depth
is reliable per brush/body, while the opaque scene depth is camera-dependent.
Wave shape therefore combines body brush depth with baked boundary distance;
scene depth remains the existing optical thickness source.

## Boundary/contact technique

The shared analytical surface is evaluated as before. Its macro height,
slope, velocity and bounded horizontal displacement are then multiplied by the
baked boundary attenuation. The existing profile `shoreline` value maps to a
small 3..8 percent rigid-edge macro residual and a 15..40 percent natural-edge
residual; the latter recovers over the inner 35 percent of the attenuation
band. Micro-normal detail is deliberately unchanged. Interactive height and
slope use the same profile and boundary signal (5..12.5 percent at a rigid
wall, 20..50 percent at a natural edge). This keeps pool contact controlled
while allowing a strong disturbance to move its visual waterline slightly.

Rigid edges use a downward overlap skirt sized from the maximum configured
wave envelope plus contact softness, capped at 32 units. Natural edges use a
short half-softness skirt whose foot extends outward by the contact width,
forming a narrow transparent wedge under the bank instead of a razor edge.
This replaces the unconditional 64-unit skirt while the feature is enabled.
Existing scene-depth thickness makes the final thin contact optically clear
instead of applying a separate shore colour. With the feature disabled, the
old displacement and 64-unit skirts are retained.

## Dynamic waterline and wet material response

The CPU evaluates the same deterministic macro terms at contact-segment
endpoints and adds the body-local interaction field. It excludes micro normals.
For each endpoint, the highest recent contact height is retained and relaxes
toward the current height over `r_waterShoreWetPersistence` seconds. The 12
segments nearest the view are selected once per view/frame and supplied to
opaque material draws. A narrow 3D distance test produces the contact fringe.

The fringe enters `lightall.glsl` as another wetness source before lighting.
It uses `R_WetnessResponse`, `weatherResponse`, automatic exclusions, material
porosity, roughness change, normal flattening and albedo darkening exactly like
rain wetness. It does not enable rain, puddles, runoff or rain ripples and works
with `r_weatherWetness 0`. Metal therefore mostly smooths, stone/concrete shows
the normal dark fringe, and `weatherResponse 0` remains dry.

The existing interaction field's B channel is the only foam destination.
Boundary cells add profile/flow/interaction-scaled foam when that field is
active. `still_pool` has a very small profile value and remains effectively
clean; no screen-space edge foam texture is created.

## Controls

| CVar | Default | Meaning |
|---|---:|---|
| `r_waterShoreline` | 0 | Master, latched; requires modern surface and render geometry |
| `r_waterShoreAttenuation` | 96 | Rigid/shallow macro recovery distance in world units |
| `r_waterShoreContactSoftness` | 6 | Contact/overlap and wet-fringe width in world units |
| `r_waterShoreWetStrength` | 0.8 | Contact source strength before per-material response |
| `r_waterShoreWetPersistence` | 8 | Retreat drying time in seconds |
| `r_waterShoreFoam` | 1 | Profile-scaled source into the existing foam field |
| `r_waterShoreDebug` | 0 | Debug view below |

Body-specific attenuation character, foam and rigid fallback come from the
existing `DynamicsProfile` (`shoreline` and `foam` members). Existing
`FoamMultiplier`, `WaveMultiplier` and `InteractionMultiplier` remain the
per-body authoring controls in `cubemaps/<map>/env.json`; no new stock asset or
PK3 format is required.

## Debug views

`r_waterShoreDebug`: 1 approximate linked-brush depth, 2 boundary distance,
3 final macro attenuation, 4 displaced macro/interaction height, 5 rigid wall
(red) versus natural bank (green), 6 water contact mask, 7 accumulated wet
fringe on opaque materials, 8 existing foam field/source, and 9 magenta crack
inspection with skirts. `r_waterGeometryInfo` additionally reports contact
segment count and CPU memory.

## Cost and limits

Map-load preprocessing is over water triangles and their boundary edges. The
water shader adds no sample: one interpolated attribute and a few scalar
operations. Wet materials test at most 12 nearest contact segments and use 25
vec4 uniforms; no texture memory is added. Nearest-segment selection is cached
per view/frame, with no per-draw allocation. Contact state is two endpoints and
four heights per retained segment.

Known limits: exact boundary matching uses 1/16-unit quantization, so a custom
map with a long edge abutting multiple unmatched short edges can retain a
conservative internal contact/skirt. Only the 12 segments nearest the current
view affect wet materials; this bounds opaque-pixel cost but can omit a distant
simultaneous fringe. Vertical waterfalls remain on the legacy path. Foam needs
an allocated interactive field, since no second foam representation is made.

## Validation results

Release SP and MP renderer DLLs build successfully. The standalone Intel UHD
OpenGL 3.2 test compiles and links all 144 water permutations and passes its
geometric displacement, interaction, depth/thickness, reflection, refraction,
Snell/TIR and numerical stability checks.

The synthetic 1920x1080 Intel UHD timer-query benchmark measured the shoreline
water path at 50.292 ms versus 49.910 ms disabled: +0.382 ms for deliberately
full-screen water. This is a stress case, not a normal frame-time estimate.

Hidden stock-map runs produced these representative measurements:

| Map | Use | Classification | Geometry preprocessing | Mesh | Contact state |
|---|---|---:|---:|---:|---:|
| `t2_rancor` | six small rigid pools, depths 8..28 | 19 ms | 12 ms | 14,784 B | 24 segments, 1,344 B |
| `vjun1` | large shallow slime body | 20 ms | 106 ms | 6,449,024 B | 467 segments, 22,752 B |
| `t3_hevil` | lake, flowing bodies and waterfalls | 20 ms | 297 ms | 17,837,952 B | 610 segments, 34,128 B |

Interactive impulses changed the ordinary rendered result on all three runs.
For `t3_hevil`, the allocated field used 1,048,576 GPU bytes and its sampled
update cost was 0.002 ms source, 6.696 ms solver and 0.943 ms upload. The larger
`vjun1` field used 2,097,152 bytes and measured 0.002/11.279/3.183 ms. These are
existing interaction-field costs; shoreline foam only adds a boundary-cell
scalar source inside that solver.

Validated PNG captures are in `docs/water-shoreline-captures`. Files prefixed
`t3-hevil-` show the lake/pillar grazing view and debug modes; the unprefixed
set is the `yavin1` natural bank view.
