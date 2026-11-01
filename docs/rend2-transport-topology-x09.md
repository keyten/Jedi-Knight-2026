# X09: independent observations and disjoint passive transport support

2026-11-01; input commit `1fdd75bb7`, branch `rend2-unified-wip`.
CPU/offline only. The old stock singularities are reproduced and removed from
the preparation path, with explicit local exclusions and incomplete transport.
**This does not establish complete stock GI, recovered stock emitters or Raven
bake parity.** Stock profiles and absolute photometry remain unknown.

## Actual cause and matched reproduction

The preserved `map-solver-v2.exe` reproduces the original failure on all three
all-pages archives: `Coincident/overlapping transport nodes; split or deduplicate
original receiving patches`. Both profile attempts stop before Joint fitting.
The old adapter creates a patch for every selected training/validation/held-out
luxel. `Transfer::Build` checks node distance before testing geometric cosines;
therefore even opposite faces on one plane abort at a shared quadrature node.

All three singular contacts are **within one fold**. They are not examples of
train/held-out duplication. The architectural coupling is still invalid: chart
sampling and partition membership cannot define physical reflector area.

Surface ordinals and page/texel coordinates below are original and zero based.
Full coordinate witnesses remain private; their hashes and identities are public.

| Map | Singular source surfaces | Luxels | Orientation and disposition |
|---|---|---|---|
| `mp/siege_desert` | 5031 / 5030 | page 11 `(74,102)` / page 11 `(0,67)` | Opposite normals, dot -1; distinct sides retained, both map in contact ROI |
| `t2_dpred` | 2284 / 179 | page 4 `(38,10)` / page 3 `(69,79)` | Opposite within float precision; source support builds, both precise receiver mappings remain unavailable |
| `t3_bounty` | 1247 / 6062 | page 21 `(94,115)` / page 2 `(64,21)` | Opposite normals, dot -1; distinct sides retained, both map in contact ROI |

There is one coincident-node pair per map. A broader selected-footprint census
finds respectively 1/2/3 opposite-side overlap pairs and 0/1/1 crossing pairs;
no certified same-side overlap among the selected receivers. These are pair
counts, not a full-map union-area census. Contact records also identify shared
original source triangles. Duplicated original geometry with opposite normals
and different materials must not be collapsed into a single reflector.
The actual desert/bounty singular pairs share original triangles; dpred's pair
does not. Dpred's tilted float footprints do not provide an exact supported
coplanar mapping after source-mesh clipping/precision exclusions. Its geometry
witness is retained for a future bounded-precision mapping implementation; no
arbitrary plane projection or distance clamp was substituted.

The matched runs retain identical BSP/archive hashes, source geometry, material
interpretation, moving bounds, original selected observation IDs and visibility
policy. The new support scope is separately declared; patch counts and texel
counts are not interchangeable. Licensed BSPs, lightmaps and OBJ coordinates
are not committed.

## Domain separation and geometry construction

`Indirect::Topology::Mesh` in `lightrecon/transport_mesh.{h,cpp}` takes source
triangles, material-side orientation, fixed spatial scope and bounded topology
options. It has no target RGB, role/fold, emitter-answer or fitting API.
`FromSurfaces` reuses the existing immutable capture.

The deterministic construction:

1. Clip actual static planar triangles to the geometry-only AABB; bisect longest
   edges. Never infer support from nominal lightmap texel size.
2. Sort by geometry/material/side/style content. Use local spatial buckets.
3. Subtract compatible coplanar overlaps; retain every contributing source
   surface/triangle reference. Physical area survives once per compatible side.
4. Keep opposite sides distinct. Exclude conflicting same-side material/style
   intersections, without choosing an arbitrary material. An uncertain front
   material does not erase a known opposite side.
5. Exclude bounded pieces with genuine interior crossings. Adjacent shared edges
   remain valid. Curved/unsupported, optical-unknown, moving/deformed and unknown
   side records remain limitations, not invented physical surfaces.
6. Validate canonical float footprints after double-precision splitting. If
   quantization creates an overlapping sliver, exclude the affected local pieces
   explicitly rather than increasing tolerances to double-count their area.

Polygon predicates use scale-dependent double roundoff bounds; degenerate edges
are removed before subtraction. This is not an optical epsilon or a distance
clamp. A `1e-9`-WU nonzero plane separation is tested and remains distinct.
The extractor requires a captured constant normal. It does not infer an extra
diffuse side from `cull none`, current PBR albedo or missing geometry.

`Joint::Observation` retains its original stable sample ID, world receiver,
normals, footprint, partition and quantized target. Its optional immutable
`Topology::Mapping` stores mesh/receiver-geometry keys, intersection areas and
Available/Partial/Unavailable status. No representative patch ID is fabricated.

For a piecewise-constant transport field:

```text
w[o,p] = area(A_observation intersect A_patch[p]) / area(A_observation)
E_indirect[o] = sum_p w[o,p] E_indirect[p]
```

Missing coverage is not hidden by normalizing weights to one. Geometry can be
shared across folds; RGB remains on the established independent evaluation side.
Reordering mappings, changing targets or repartitioning observations cannot
change support area/key. Stale receiver or mesh generations are rejected.

Direct local/sun/sky illumination is evaluated by the production forward operator
at the **original observation receiver**. Only indirect illumination is mapped
from transport patches. Compatible pre-encoding terms are combined before output
encoding; encoded contributions are not added after nonlinear normalization.
Existing patch-index measurements remain valid for explicitly defined disjoint
patch measurements in controlled CPU fixtures.

## Reciprocal transfer and honest near-field uncertainty

```text
Cij = integral_Ai integral_Aj V(x,y) cos_i cos_j / (pi r^2) dAi dAj
Fij = Cij/Ai;   Fji = Cij/Aj;   Cij = Cji
E1_i[root,c] = sum_j Cij/Ai * rho_j[c] * E0_j[root,c]
```

One stored unordered link enforces reciprocal conductance. C has world-area
units and F is dimensionless. Source roots, passive reflectance, RGB and bounce
membership retain the S10/S11 contract. No synthetic emission is added per patch.

Ideal coplanar Lambertian patches have identically zero cosine product, including
opposite sheet sides. Their zero diffuse exchange follows from this integral;
it is not proof of transparency. Non-coplanar zero-distance nodes remain
unsupported; r is never replaced by a constant epsilon.

Troublesome pairs receive bounded subdivision/refinement. If unresolved, the
link has nominal C=0, possible capacity at most min(Ai,Aj), and explicit
`UnresolvedQuadrature|OmittedLinks`. That nominal zero is **not known zero GI**.
Energy-inconsistent outgoing rows invalidate affected reciprocal estimates;
they are not rescaled to unit energy. Possible pair capacities are not jointly
attainable row assignments; existing root-power upper bounds still apply.

The refined-rule difference is an indicator, not a certified integration bound.
Unknown optical/moving/approximate traces stay unknown. Known reciprocal links
require clear forward and reverse BVH queries; opaque obstruction stays blocked.

`allowPartialWork` explicitly permits a partial result from this optional GI
stage's local work/ray/pair cap. Only completed links survive; search completeness
and omitted transport remain explicit. Default behaviour stays strict.
Cancellation, corrupt/invalid input and memory failures remain hard errors.

`Joint::Request::directOnlyConditional` is a separate **whole-model** incomplete-GI
hypothesis. It never drops GI for individual rows as source parameters change.
Predictions/scores record MissingTransport/direct-only counts. Strict GI requests
do not turn missing mappings into black measurements. Stock continuation uses
this explicit direct-only hypothesis, not a claim of successful stock GI fitting.

## Measured coverage, area and numerical results

The main scope is the prior geometry-selected 512-WU cell (normal diversity,
then area, then lexicographic cell), selected before folds/RGB, with 128-WU
maximum edges. Additional 256-WU boxes cover the actual old singular contacts.
These are bounded spatial experiments, not full-map GI meshes.

| Map | Old observation patches | New main patches | Input side area WU² | Retained WU² | Deduplicated WU² | Unsupported/withdrawn WU² |
|---|---:|---:|---:|---:|---:|---:|
| `mp/siege_desert` | 130 | 3,167 | 2,073,400.782 | 1,742,440.042 | 0 | 330,960.740 |
| `t2_dpred` | 922 | 1,937 | 1,896,176.979 | 1,821,004.124 | 0 | 75,172.840 |
| `t3_bounty` | 527 | 1,936 | 2,011,039.506 | 1,886,235.444 | 2,078.530 | 122,725.561 |

Input/withdrawn areas use source-side multiplicity; retained area is disjoint per
compatible side. Conflicting alternatives are both charged as withdrawn. Signed
area-accounting residuals are -0.000839, +0.014950 and -0.030552 WU², recorded
separately instead of renormalizing areas. Precision exclusions are 15/0/15 pieces.
Main-ROI interior-crossing pair counts are 194/36/74; they describe a different
domain from the selected-luxel contact census.

Main-ROI mappings are **0/130, 5/922, 5/527**. Covered area is 0, 1,248 and 3,840
WU² out of 97,825.572, 570,490.209 and 377,572.492 WU² selected observation area.
This low shared coverage is an explicit limitation of a compact independent
support scope with sparse whole-map receivers. Contact ROIs admit both original
receivers for desert/bounty; dpred remains a specific unsupported mapping case.

Main transfers retain 13,815 / 11,588 / 12,291 links, with 449,548 / 425,613 /
474,446 directional rays. All reach the local 128,000,000-work cap. **No positive
certified stock GI field is established:** known nominal outgoing fractions are
zero, while uncertain/possible conductance and omitted support remain. JSON
records uncertain links/rays and incomplete search; nominal zero is not a
measured absence of reflected light.

Controlled results:

- Duplicate same-side quads: input 2 → physical area 1, deduplicated area 1,
  preserving both source references.
- Half-overlapping charts: input 2 → union 1.5. Opposing sheet sides: total side
  area 2, never merged. A front-material conflict does not erase the rear.
- Crossing surfaces plus unrelated support: input 3 → retained 1, unsupported 2.
- Independent finite-square convolution: reference `0.199824436064`, reciprocal
  result `0.199981958715`; absolute error `0.000157522651` (~0.079%), 559,240 rays.
  Declared tolerance is absolute error <0.00025, not exactness. Insufficient
  near-field budget yields an uncertain capacity, not a clamped response.
- Positive red bounce, passivity, sealed-wall/doorway regressions, independent
  direct receiver prediction, positive mapped red GI, held-out geometry reuse,
  stale mapping rejection and whole-model direct-only fallback are tested.

| Map | Total process seconds | Actual peak RSS MiB | Main mesh seconds | Main transfer seconds | Accounted topology peak MiB |
|---|---:|---:|---:|---:|---:|
| `mp/siege_desert` | 12.266 | 58.02 | 1.083 | 2.158 | 18.00 |
| `t2_dpred` | 13.578 | 86.23 | 0.327 | 2.685 | 10.96 |
| `t3_bounty` | 14.609 | 113.76 | 0.702 | 3.812 | 12.36 |

The full producer continuation was also actually run:

| Map | Joint reached? | Outcome for each conditional profile | Recovered stock sources |
|---|---|---|---|
| `mp/siege_desert` | Yes | 96 attempted/rejected models: 95 invalid support/path/bounds, one `Shadow source count`; mandatory structural budget exhausted | None |
| `t2_dpred` | No | Later independent `Sun/sky hint budget` preflight failure | None |
| `t3_bounty` | No | Later independent `Sun/sky hint budget` preflight failure | None |

The last two are existing operational provider-input limits, not evidence of no
sun and not GI topology failures. They remain hard failures. Desert reaches model
admission but no candidate passes to numerical forward fitting in that run
(zero forward calls). None has accepted stock held-out/source-accuracy metrics.
The former singularity is repaired/narrowed; the complete stock solver is not
reported as successful.

Actual process RSS, stage timings, hashes, contact outcomes and producer
continuation are in the [versioned JSON package](../tools/rend2/transport_topology/results/v1/manifest.json).
Time/RSS are outside deterministic numerical comparisons. Process RSS includes
archive load/census/visibility, not just accounted transport vectors.

## Validation and versions

- New suite: **148 assertions**, zero failures, identical repeated numerical
  witnesses; about one second, actual peak working set about 4.6 MiB.
- CPU CTest: **31/31 pass, 83.52 s** after fixing the algorithm/wire-version
  mismatch found by the initial run. The unchanged expensive standalone
  `lightrecon_point_refine` suite was not rerun. Region, visibility, surfaces,
  offline/BSP, forward, amplitude, shape, GI, Joint, uncertainty, producer/cache/
  publication and lifecycle checks ran.
- SP/MP MSVC 2019 x64 Release builds pass; GCC 16.2.0 Release builds standalone
  tools. No renderer lighting/shader/consumer changes.
- Three topology result tests, six blind-view tests and four audit tests pass.
  The architecture audit now includes transport_mesh.
- Disabled source/binding comparison versus `1fdd75bb7`: zero differences,
  27 programs. The earlier GL-3.2 compile record is reused. No fresh GPU/game
  visual validation is claimed; historical shader-baseline drift is retained.
- Development failures remain private: degenerate polygon subtraction and a
  temporary 1-GiB process cap, then representational overlap failures before
  precision exclusion. They are not passed runs.

Topology version 1, passive algorithm version 2, Joint version 3. Keys include
map/material/side geometry, scope/options, visibility, receiver geometry,
represented links and partial-work status as appropriate. Old dependent caches
are invalidated. Canonical schema stays 5; `.jkobs` is unchanged. The unchanged
passive field wire layout remains **payload version 1**, separate from algorithm
version; a byte-header regression and producer round trips cover it.

## Remaining limits and reproduction

The existing producer payload assumes one observation per patch. A many-to-many
mapped request therefore stays **diagnostic-only and NotCacheable** at publication,
retaining numerical/final diagnostics; it is never serialized with the old
meaning. Versioned mapping payloads and scene-wide support scheduling are follow-ups.

Further stock GI needs reliable non-axis-aligned float support, supported curved/
optical/material semantics, fold-independent spatial scheduling aligned with
receivers, and cheaper link integration. Main scopes currently have low shared
coverage and incomplete visibility. More patches alone do not establish correctness.
No original stock source or complete positive stock GI has been certified.

Exact commands and private witness descriptions: [tool guide](../tools/rend2/transport_topology/README.md).
No game, new q3map2 bake or asset recapture was needed for X09.
