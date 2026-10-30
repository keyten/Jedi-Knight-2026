# S10: passive indirect illumination and unresolved transport

CPU/offline implementation, 2026-10-30, following the decision in
[inverse-GI research](rend2-inverse-gi-research.md), particularly G4–G8 and the
material/profile/energy counterexamples. The implementation does not create
physical emitters, alter source XYZ/RGB, run during ordinary map loading, or
change runtime lightmaps, PBR, entities, fog, LTC, sun or shadows.

## Implementation and explicit scope

`shared/rd-rend2/lightrecon/indirect.{h,cpp}` provides:

* `Transfer::Build`: immutable sparse geometry-only transfer with actual mapped
  planar receiver footprints, material/side/style identity, static BVH queries,
  explicit moving bounds, bounded work and content-dependent identity.
* `PredictRoot`: a batched adapter to the production `Bake::CpuForwardEvaluator`
  for supported historical **pre-encoding surface** contributions. Local emitter,
  sun and sky roots remain separate. Ambient inside a per-source recipe is refused
  to avoid duplicating it. Physical-kernel callers can supply frozen `Root` values
  from the existing S08 operator; the tests exercise that production route.
* `Fit`: bounded material-group refinement at **fixed primary illumination**,
  then optional coarse nonnegative unresolved appearance fitting. There is no free
  bounce gain and no source intensity/location parameter in this stage.
* `EvaluateFrozen`: independent validation/held-out interval metrics and a streaming
  diagnostic sink, called after fitting. The fitting request accepts training
  records only; role mismatch, corrupt/short streams and invalid identities fail.
* `AppendFields`: canonical `TransportField` descriptors and bounded portable
  chunk payloads. It never appends to `PhysicalEmitter` or publishes an asset.
* `DebugTsv`: direct-only, direct sun, sky, ambient, indirect, unresolved and total
  values, availability and reason bits. Signed interval discrepancy is separate
  from all nonnegative energy-like fields.

The historical adapter deliberately excludes grid ambient/directed, deluxe and
vertex observations. They need their own transport/observation operators; surface
irradiance is not a substitute. Unsupported domains return `Unavailable`.
The optional region graph is checked for matching map/visibility generations.
Region IDs, PVS and disconnected sampled components never prove optical visibility.
Locally supported geometry does not require certified region membership.

## Passive transport

For disjoint diffuse patches with original world areas A, use one reciprocal
geometric conductance per represented pair:

```text
Cij = integral_Ai integral_Aj V(x,y) cos_i cos_j / (pi r_world^2) dAi dAj
Fij = Cij / Ai;  Fji = Cij / Aj
E1_i[root,c] = sum_j Cij / Ai * rho_j[c] * E0_j[root,c]
E0 = local-direct + direct-sun + direct-sky + separately identified ambient
```

`Cij=Cji` enforces reciprocity; known outgoing row fractions must not exceed one.
The builder rejects a violated pair/row energy budget and requests finer supplied
patches. It never rescales an invalid row to one or clamps a near-field inverse
square singularity into a plausible answer. Roots and RGB channels propagate
separately, so coloured bounce and overlapping sources have explicit origins.

Area/distance units cancel in the transfer. Primary illumination must already be
in one declared compatible incident domain: source-linear relative,
compiler-relative-255, explicitly calibrated scene-equivalent lux, or authored
physical lux. Scene-equivalent values retain unknown absolute scale; a mapping
method and matching `LegacySceneEquivalent` calibration are required. There is
no automatic gain or physical-to-scene inversion. Root and contract calibration
IDs/domains must match. There is no conversion from q3map intensity to candela.

Triangle midpoint subdivision uses a declared rule and a refined rule. The latter
supplies the value; their discrepancy is recorded. Neither rule is a rigorous
visibility/integration bound. Curved normal variation requires a different
quadrature and is refused; geometry-error envelopes remain uncertain.

A fixed spatial hash enumerates nearby candidate patch pairs. Storage is a sparse
pair list, never an obligatory dense all-surface matrix. Application is linear in
represented links per root. The distance cutoff and incomplete patch set are
explicit **omitted transport**, not certified escape/zero energy. Upper estimates
are marked unavailable when missing incident energy or omitted reflectors prevent
a finite supported statement. No nearest-N cutoff silently discards distant light.

Opaque ray hits give zero link response. Unknown optical/moving/patch paths give
a conditional visibility range, never assumed transparency or 0.5 transmission.
Each reciprocal quadrature pair is traced in both directions. Two geometrically
blocked directions give zero; two clear directions permit known conductance.
One-sided material culling disagreement stays uncertain rather than asserting
physical reciprocity from a single ray. The coarse unresolved field applies the
same two-direction clear-path requirement, including its endpoint offset guards.
Possible incoming power is capped by available reflected root power, not by an
invented local source. These intervals are conditional material/visibility sets
under the quadrature model, not calibrated probabilities or certified numerical
error bounds. Incomparable full predictions remain unavailable to RGB scoring.

The first implementation is one bounce. It does not reproduce q3map's empirical
polygon-light bounce algorithm, solve higher orders, or infer bounce order from
one bake. An energy-conserving physical approximation can disagree with an actual
legacy compiler; such a discrepancy must remain visible.

## Material and observation fitting

Each material group is Known, Bounded or Unknown. Known values have equal bounds.
Bounded RGB reflectance stays in `[0,1]`; initial values and optional quadratic
priors have explicit provenance. Unknown material has no asserted central
reflectance: nominal/lower reflected energy is zero with an explicit upper
hypothesis. Today's PBR texture is never substituted or divided into lightmap RGB.

For permitted material parameters rho, minimize an area-normalized robust interval
objective at frozen roots:

```text
L = sum_i Ai / sum_eligible A * sum_c Huber(dist(O(E0+K_rho E0), bin_i,c)/sigma_i)
    + sum_material priorWeight * ||rho-rho_initial||^2
```

Bounded deterministic coordinate searches use the full observation operator,
including coupled RGB encoding. They return stationary/iteration/budget status,
not a claim of global optimum or calibrated material covariance. Endpoint
alternatives within the declared objective slack are retained. Material allocation
identifiability and physical-bounce verification remain false by default.

Supported observation contracts are incident linear, outgoing Lambertian
`rho_receiver * E/pi`, and explicit NetRadiant encoded output. For encoded
observations, sum compatible components **before** the existing production
gamma/compensation/shared-peak transform. Truncating RGB8 bins and censored 255
channels are compared in output space. Independent clipping inversion is not used.
Unknown transfer/primaries/operator leave observations incomparable. An explicitly
assumed physical transport model can be tested against a historical profile but
does not become verified compiler bounce replay.

The direct-model/profile, material, visibility, quadrature, missing coverage,
relative scale and measurement limitations are separate. A wrong direct model
does not authorize a free positive GI gain. The stage exposes its residual and
profile-conditional alternatives rather than labeling the entire difference bounce.

## Optional unresolved appearance

The fallback is opt-in and currently supports declared incident-linear comparison
only. A fixed world origin/cell size groups compatible surface-side/material/style
samples. It has a nonnegative upper value, cell/byte/work limits and a strictly
positive magnitude penalty. It is fit **after** direct roots and material values
freeze. Spatial smoothness is not implemented; a nonzero smoothness request fails
instead of pretending to provide a graph regularizer.

Only cells with training support may extrapolate. Surface-normal endpoints are
offset through independently guarded short segments, followed by a full BVH path
check. Unknown or blocked paths, changed normals and absent training anchors do
not connect. The explicit fixed grid origin matters: a held-out cell without a
training anchor is unsupported, not permission to move the grid after seeing
its RGB. This is conservative appearance interpolation, not proof of an energy
origin or a complete room/doorway interpolation method.

It cannot suppress emitters because it has no access to source amplitude updates.
A future alternating controller must rebuild primary roots when sources move,
preserve direct-anchor/source-removal tests and select complexity on validation.
Running such a controller is outside S10; no unconstrained per-luxel residual or
automatic joint direct/GI loop was added.

## Canonical membership, format and cache implications

Canonical schema **2** adds `TransportMembership` with model identity, root
references/kinds, Included/Excluded/Unknown flags for direct local/sun/sky,
ambient, reflected and unresolved content, explicit reflection order and style.
`UnresolvedIrradiance` distinguishes unknown-origin appearance from verified bounce
or direct light. Consumers must resolve Unknown membership before composition.
The direct/local/sun/sky and legacy-ambient flags describe zero-reflection terms.
The roots separately describe the origin of reflected energy: a sky-rooted
one-bounce field excludes direct sky while retaining that sky origin. Likewise,
bounced legacy ambient is not an instruction to add the original fill twice.

The disk envelope remains format 1.0, but its semantic schema field is now 2;
schema-1 assets are rejected. Content keys include the schema, so old cache keys
are not reused. The observation `.jkobs` schema/bytes and normal rendering are
unchanged. The sparse-field validator was corrected: logical element count is
compared against lattice dimensions only when a lattice is actually specified.

S10 field encoding is `org.openjk.passive-transport-samples/DOMAIN`, version 1,
where DOMAIN is `source-linear-relative`, `compiler-relative255`,
`scene-equivalent-lux` or `physical-lux`. These encodings are distinct; a reader
must not treat their payload RGB as sharing an unstated brightness scale. Each
little-endian chunk has u32 magic `0x3154474a`, algorithm version, record count
and component. Each record contains u64 patch ID, u32 reason bits, u8 availability,
u8 upper availability, u8 style, reserved zero, followed by binary32 world XYZ,
geometric normal, nominal RGB, lower RGB and upper RGB. Sparse absence is unknown.
Chunks contain at most 128 records. Standard disk checksums protect the bytes;
membership/provenance remain canonical metadata. Field export requires one
explicit style partition and matching roots/provenance/calibration in the asset.

These are candidate transport fields, never resolved runtime lights. The API
caller owns all leases and output publication; no renderer or consumer is enabled.

## Validation and limitations

The owned C++ suite tests red-to-white bounce, nonnegative material recovery,
multiple source roots, sealed wall/open doorway, missing/alpha/moving geometry,
sky-rooted reflection, GI-filled direct shadow, ambient-only lighting, unknown and
conflicting albedo, quantized/clipped RGB, wrong direct model, held-out isolation,
unresolved appearance, canonical serialization and bounded failures. The producer
does not create emitters by construction; zero added emitters is not a source
detection/recall score.

Final deterministic witness: **338 C++ assertions, zero failures**, repeated twice.
The floor split uses training Y=-1,-0.5 and held-out Y=+0.5,+1 with .15-unit
receiver footprints. Reflector RGB starts at `(0.3,0.3,0.3)` under bounded material
priors; fixed source illumination is supplied, not recovered by this stage.

| Controlled comparison | Result / interpretation |
|---|---|
| Coloured reflector | Fitted rho `(0.700000345707,0.180000007153,0.0800008699298)` versus `(0.7,0.18,0.08)` |
| Same held-out receivers | Transport MSE `1.33226762955e-16`; direct-only `3.98462528274e-5`; black `0.233308001829`, relative linear RGB squared |
| Fixed wrong direct model | Training robust objective `9628.97`; remains an incorrect conditional hypothesis, never verified GI |
| Sealed wall | Receiver first-bounce contribution 0 |
| Opening, known incident boundary | Receiver red first bounce `0.00557812629268` |
| Alpha / missing / moving blocker | Nominal 0, possible-model upper `0.00557812629268`; full RGB prediction incomparable, not a black measurement |
| One-sided wall | Opposite ray directions disagree; reciprocal conductance remains unknown, no unresolved-field extrapolation across it |
| Actual hidden point source + reflecting ceiling + doorway | Receiver direct 0, reflected red `0.00197298172861`; no aperture emitter |
| Coarse unresolved fill, frozen roots | Held-out MSE `6.9041069158e-14` versus no-field `0.0174999991556`; source amplitudes unchanged |

The source-linked toy truth uses the same transfer discretization for its exact
material-recovery assertion. The independent finite-area comparison below is
reported separately; near-zero MSE is not evidence of arbitrary scene accuracy.
The constructed free-per-observation residual baseline admits zero direct light
and zero training error; the existing 15-test inverse-GI research suite also
reruns its actual active-set overfit/held-out counterexamples. Production S10 has
no such per-observation free residual or source-amplitude variable.

The 26-patch coloured scene represents 25 reciprocal links, builds with 3400
visibility rays and uses 345 material objective evaluations. The whole owned
suite took 0.216–0.220 seconds per GCC Release repetition with about 4.49 MiB actual
Windows peak process RSS. These include owned geometry/preparation/fitting/tests;
they are not per-map stock timings or a 50k-patch memory guarantee.

All **26** GCC CPU CTest cases passed: 25 in the final parallel run (16.25 s),
S07 separately (448.03 s on the shared machine). MSVC indirect/disk tests and
SP/MP Release builds passed. Python passed 3 new gates and 15 research regressions.
Twenty supported compiled forward cases retained their predeclared tolerances;
two bounce replay cases stayed explicitly unsupported. The shader input/binding
comparison matched 27 programs / 51 stages; it was not a GPU compilation.

The initial disk regression failed its schema-1 byte witness as expected after
the explicit schema bump. The schema-2 empty-asset SHA-256 is now pinned as
`935a80789be1e31acb3c9aa22342e2d16e99946087508d0ab35dafac141d34a4`;
a valid-checksum schema-1 envelope is explicitly rejected. Other corrected test
issues and exact logs/commands are retained in `results/validation.json`.

An independent dense finite-rectangle integral at receiver centres checks the transfer approximation.
The selected scene's maximum relative discrepancy is about 0.282%; the 48/64-rule
reference difference is about `1.06e-7` relative incident units. This small-patch
fixture also compares point-receiver reference values with area-averaged patch
values; its discrepancy is not a pure certified quadrature error. It does not
establish arbitrary near-field accuracy. A violated passivity
budget fails rather than being hidden by normalization.

Existing paired q3map2 bakes are reused with their original hashes and mapping
checks. Positive coloured bounce, doorway transport and a dark sealed side remain
diagnostics in encoded RGB8. The skylight recipe's zero additional encoded bounce
remains a valid negative control. S10 does not claim a physical replay of these
compiler results, nor identify Raven/stock bake profiles.

Final numerical results, test/build commands, work/ray counts, peak process RSS
and unperformed checks are recorded in `tools/rend2/indirect_fit/results/`.
Reproduction commands are in that tool's README. No licensed stock assets are
distributed. Stock material/profile/patch uncertainty, specular transport,
multiple bounces, unrestricted scene patch segmentation and a general full-map
transport hierarchy remain later work. The first implementation consumes bounded
mapped patch sets; it does not silently run an all-map radiosity build.
