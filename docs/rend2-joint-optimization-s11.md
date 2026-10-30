# S11: coordinated offline lighting hypotheses

Implementation date: 2026-10-30. `lightrecon/joint.h/.cpp` coordinates the
production S08 source operator, S07 placement certification, S09 shadow objective
and S10 passive transport. It does not register a runtime algorithm, publish
resolved lights, overwrite a lightmap, or enable a renderer consumer.

This is a bounded joint solver over the **admitted observation/patch set**.
It is not evidence that whole stock JA maps now have uniquely recovered lights.
Stock bake profile, reflectance, absolute scale and optical support remain
conditional. A numerical `Converged` state is not physical certification.

## Objective and domains

For a discrete profile/calibration hypothesis h, source set M, source parameters
theta, and material-group reflectances rho:

```text
E0[root] = production source evaluation(theta[root], receiver geometry, h)
E1[root] = K_rho E0[root]                    # first diffuse reflection only
stored_surface = O_h(sum_root(E0 + E1))     # encode the complete sum once

L = sum_channel w_channel mean_region/block(
        sum_i area_i mean_present_channels Huber(interval_distance(pred_i, bin_i)/sigma_i)
        / sum_i area_i)
    + w_worst max_block(normalized robust block loss)
    + w_shadow * automatic_source_conditioned_shadow_loss
    + fixture_pose/size_prior + material_prior
    + lambda_source * source_count + lambda_parameter * free_parameter_count
```

Intervals use the existing `Amplitude::Rgb8` convention, including the censored
255 channel and profile-specific shared-peak encoding. RGB components are added
before encoding; encoded components are not added or subtracted as energy.
Black channels, missing channels and unavailable geometry have different states.

The grid ambient/directed and directional operators remain separate production
`Bake::CpuForwardEvaluator` requests. Their residuals use their own sigma and
weight; a radian residual is never silently averaged into squared RGB8 bytes.
`Score::intervalMse/blackMse/worstBlockRmse` refer to surface RGB; all channel,
region and block results remain individually available in `Score::groups`.
Disabled and incomparable records have explicit coverage counters.

Repeated texels in one block do not increase its total objective weight. Surface
area still sets weights inside the block. Region IDs organize evidence/proposals;
they never suppress a source contribution or certify an optical path. Every
active source can illuminate every admitted receiver, including another region.
There is no PVS shortcut and no permanent region-local lighting solution.

Legacy Q, relative physical integrals, scene-equivalent lux and authored SI
remain different contracts. A single global brightness gauge is fixed per
explicit `Profile::fixedGain/calibrationHypothesis`. Competing gains can be
compared discretely; unrestricted gain and unrestricted RGB are not fitted
together. A preferred gain/profile is not historical compiler identification.

## Passive GI and four output meanings

The S10 transfer convention is unchanged:

```text
Cij = integral_Ai integral_Aj V cos_i cos_j / (pi r_world^2) dAi dAj
Cij = Cji; Fji = Cij/Aj; sum_i Fji <= 1
E1_i[root,c] = sum_j Cij/Ai * rho_j[c] * E0_j[root,c]
```

Original clipped planar footprints, geometric sides, world areas, two-direction
visibility certification and sparse transfer budgets come from S10. Invalid
energy rows fail; no row normalization invents energy. Unknown materials require
an explicit conditional bounded hypothesis. Modern PBR base colour is not
substituted for bake-time reflectance. Missing links and uncertain optics retain
their S10 limitations. A locally clear ray does not need a certified region ID.

Every model keeps per-root `RootPower` records for local lights, sun, sky and
identified ambient. First-bounce power is separate from zero-reflection terms.
The power diagnostics are sums of incident-domain RGB times **world area**;
they are not unnamed watts, lumens or calibrated stock photometry.

For a complete supported patch set and rhoMax < 1, the omitted higher-order total
power bound is `P0*rhoMax^2/(1-rhoMax)`. It is conditional on the patch/transport
model, not a per-luxel bound, exact q3map replay or justification for adding
higher bounces. High-reflectance controls deliberately have a large tail bound.

The solver keeps four meanings separate:

1. Profile-supported direct local/sun/sky values, with their applicability assumptions.
2. Conditional passive source-rooted first-bounce transport.
3. Approximate unresolved incident illumination, represented separately. S11 does
   not give this component free parameters; the optional bounded S10 stage may
   run after sources freeze. Unknown-origin roots are refused rather than being
   relabelled ambient or bounced physical energy.
4. Signed observation-interval discrepancy. `Prediction::signedDiscrepancy` is a
   comparison-domain residual, not a positive incident-radiance field.

With no source/boundary illumination, passive GI is zero. A source-free field
cannot suppress a real source by taking over its bright texels. Identified
ambient is an explicit fixed boundary root. Wrong-profile discrepancy is not
automatically designated GI. No synthetic doorway, GI or negative lights exist.

## Search, refitting and alternatives

`Joint::Solve` performs:

1. Validate immutable map, policy, geometry, profile, footprint and training keys.
2. Evaluate no-source/boundary-only controls and supplied initial-active sets.
   If a directional black baseline is undefined, bootstrap a supported candidate;
   undefined black direction must not prohibit all positive hypotheses.
3. Generate deterministic activation/removal and mutually exclusive family/type
   alternatives. Interleave candidates by region, root and shape class to keep a
   large fragment class from occupying the entire proposal queue.
4. Generate bounded point merge/split proposals as **competing models**. Derived
   IDs and parent references retain their origins. Distinct fixtures are not
   overwritten or silently deduplicated.
5. Optionally search a predeclared geometry lattice where training residuals are
   positive. Position and probe amplitude bounds are declared, placement is
   certified, and screening reads no validation/held-out RGB. These are proposals,
   not emitter evidence. The existing S04/S05 providers can supply better initial
   catalogues; S11 does not rebuild their capture pipeline.
6. Refit source RGB, bounded material parameters and permitted geometry for every
   count/type model. A source removal always gets a complete refit; an L1 nonzero
   coefficient count never becomes source-count certification.
   An optional block-stratified coarse observation budget must retain every
   evidence block; the strict stage always restores all training observations.
7. Reevaluate strict sampling, refit and refine on that value. Update visibility
   after XYZ/frame/extent changes. Final model scoring does not reuse a frozen
   source-dependent shadow mask.
8. Request aggregate validation scores from a trusted controller, retain a bounded
   beam, and preserve models with comparable unpenalized validation evidence.
   Complexity chooses a preferred representative; it does not erase equivalent
   one-versus-two-light explanations.
9. Freeze parameters before external final held-out prediction/source evaluation.

Source geometry uses bounded derivative-free pattern steps. Frames are rotated
on SO(3); inner/outer physical cones remain ordered. Historical spot radius/target
has its own law. Dimensions stay positive and within fixture limits. Placement
checks positive clearance volumes and conservative extended-source support bounds;
it does not certify an area source from a free centre alone. These conservative
bounds can reject a valid recessed/large fixture and report unsupported placement.

RGB fitting uses a small matrix-free coordinate method. A production
`Shape::Block::Linear` proof is required before using one temporary response
column. The complete allowed amplitude box, domain and fixed material state
must be linear. Each proposed coefficient update is still checked by the complete
operator. Nonlinear historical encoded/FAST branches use bounded value searches,
endpoints and deterministic restarts. These are local methods, not a proof of a
global minimum. Parameter stagnation, a work cap and a physical contradiction
have different statuses.

Source/fixture prior weights and complexity penalties are explicit caller policy.
No universal intensity-magnitude penalty suppresses dim lamps. Required block
validation and independent colour/shadow support can retain a weak local source
even beside a much brighter one. Validation gates are frozen per scenario;
compact relative fixtures use a .015 block RGB RMSE gate, compiler adapter controls
use 2 RGB8 codes. Neither number is a universal stock-map reconstruction criterion.

| Joint operation | Implemented numerical scope | Explicit limitation |
|---|---|---|
| Historical surface RGB | Selected NetRadiant rules; point/spot, explicitly enabled polygon facet expansions, sun/sky | General static visual tracing is conditional; no blanket Raven parity |
| Physical incident/outgoing surface | Point, smooth spot, rectangle, two-sided rectangle, open finite cylinder/tube, S08 quadrature | Not automatically the historical baker's observation law |
| Passive reflected component | S10 sparse first bounce, fixed/bounded material groups, source roots | No arbitrary patch emission, empirical q3map bounce replay or fitted higher order |
| Grid ambient/directed/dominant | Production unpacked point/spot rule, direct-only | No ordered compression replay, surface-GI substitution, sun/sky/area grid recipe |
| Deluxe direction | Production supported point/spot direction rules | No asserted sun/sky/polygon deluxe parity or GI contamination model |
| Shadow objective | S09 automatic association, declared surrogate/reference scope | Its assumptions and ambiguity survive in the result |
| Final strict evaluation | Full training density and strict S08 sampling, reference shadow call | “Strict” does not widen a compiler-specific replay's verified scope |
| Source styles | Current S08 style-0 scope | Other styles remain unsupported, not silently merged |

Sun/sky initial alternatives may be supplied by S02; the joint stage can refine
permitted sun orientation and RGB while keeping sky RGB distinct. The controlled
local+sun comparison supplies the directional prior; its zero angular error is
not evidence of a new blind sun-direction discovery experiment.

## Diagnostics, identifiability and resource policy

`Model` exposes strict reevaluation, objective terms, per-region/block validation,
per-root power, material assumptions, local RGB Jacobian singular spectrum and
conditional alternatives. The bounded Jacobian diagnostic uses production finite
differences at frozen geometry/material/profile. It does not identify source XYZ,
source count, material scale or existence. `physicalExistenceCertified` and
`sourceAllocationIdentifiable` remain false; optimizer convergence never sets them.

The search records attempted models, reasons, all forward evaluations, failed-model
native rays, traversal, arithmetic, cache hits and deferred proposals. Geometry
blocks are source-amplitude-independent only where the production implementation
allows it. Keys include map/transfer generations, geometry, profile, sampling,
source pose and interpretation. There is no global cache of all ray pairs and no
mandatory dense all-source/all-luxel matrix.

Hard limits bound candidates, active sources, beam, proposals, observations,
model metadata, cache, evaluations, rays, arithmetic and elapsed-time safety.
The time cap may stop at different points on a busy machine; deterministic work
caps define reproducible numerical runs. Full-map observation tiling/candidate
waves must be scheduled by an offline host; this implementation is not a new
automatic hierarchical full-stock-map radiosity system.

Checkpoints preserve completed models, search round, seeds/policy identity and
immutable geometry-only cache leases. They support deterministic cooperative
**in-process** round-boundary resume. No portable crash-resume file codec was
introduced; the existing session infrastructure does not provide one. Cancelled
or budget-limited calls return explicit status and diagnostics, never resolved
lighting. Stale map/policy/partition checkpoints fail.

## Blind evaluation and native compiled adapter

`Request::training` is the only RGB reader passed to fitting. Role mismatch fails.
`Validation` exposes an opaque partition key and aggregate `Score`; it does not
provide a validation RGB buffer. `EvaluateFrozen` accepts validation/held-out
records only after strict parameter freezing. This is an API/data boundary;
untrusted native solvers still need an OS account/container boundary.

`lightrecon_joint_tool` adds a small native controlled-fixture adapter. It reads a
sanitized `.public.jkobs` geometry archive, source XYZ priors and training values.
It checks that original lightmaps, retained emitter evidence and vertex/grid RGB
are absent. Actual receiver footprints are clipped against the original source
triangle UVs, and their areas are checked against exported mappings. Validation
and held-out sections contain geometry only. A separate Python controller scores
validation predictions and sends scalar metrics over stdin. Final held-out RGB
and private source amplitudes never enter the native process.

The compiled controls reuse the pinned original q3map2 BSPs and X07 spatial-gap
views. Supplied XYZ/count/type are benchmark priors; those runs do not demonstrate
source discovery. Native S11 does perform model activation/removal/amplitude
fitting. The owned missing-candidate controls exercise separate discovery proposals.

## Validation record

The final owned witness has **246 active Release checks across 27 case records**.
Two runs have identical semantic JSON. The fixed-work small suite takes
11.10–12.02 seconds with 6,000,640–6,008,832 bytes peak process working set
(about 5.73 MiB). These are process measurements on the compact suite, not a
full-map resource guarantee. Transfer preparation precedes solver work counters;
process time/RSS include fixture loading/preparation. `Work::rays` includes native
fitting/validation rays and placement/shadow work, including rejected models.
All 27 standalone CPU targets passed (26 in the final parallel run, plus the
204.47-second final S07 regression). SP/MP Release builds and the three targeted
MSVC joint/indirect/disk tests passed. Python passed 4 joint gates, 39 evaluation,
6 blind-view and 15 inverse-GI research tests. The 22 original forward comparisons
retained 20 supported passes and two explicitly unsupported bounce replays; the
scoped `multi_sun` regression remained exact. Shader input/binding comparison
matched 27 programs/51 stages; no GPU compilation or game launch was performed.

| Owned experiment | Held-out surface MSE / black MSE | Preferred source count / retained models | Meaning |
|---|---|---|---|
| Two colours, coarse-to-full data | `1.24441e-6 / .756034` | 2 / 1 | Both sources retained; geometry priors supplied |
| Same colour | `5.63604e-10 / 2.22314` | 2 / 1 | Distinct response patterns support both candidates |
| Point + spot + panel | `2.78876e-5 / .120585` | 3 / 1 | Correct controlled type/count at supplied geometry |
| Bright red + weak blue | `1.87715e-10 / 2.08960` | 2 / 1 | Weak source survives; no brightness-only deletion |
| Red reflector | `1.92439e-15 / .434379` | 1 / 2 | Rooted passive bounce; alternative extra-zero candidate does not establish existence |
| Missing initial candidate | `2.15826e-12 / .415763` | 1 / 4 | Training-only lattice proposes a useful source; spatial alternatives survive |
| Coincident two-source truth | `1.33982e-12 / 1.29626` | 1 / 4 | One/two-source models remain equivalent; preferred literal recall is 1/2, not falsely 100% |
| Unknown absolute gain | `2.32986e-12 / .376395` | 1 / 2 | Gains 1 and 2 survive with compensating Q; absolute intensity is incomparable |
| Spot axis, cone explicitly known | `5.06940e-5 / .186327` | 1 / 1 | Bounded joint refinement, still partial numerical convergence |
| Panel dimensions | `2.97436e-11 / .00385267` | 1 / 1 | Fitted dimension relative error about .252% |
| Tube dimensions | `4.49688e-9 / .00122109` | 1 / 1 | Dimension relative error about 7.69%; low RGB error does not imply exact extent |

Units in that table are the declared relative incident RGB squared. Most fits
are honestly `Partial`: finite alternating/pattern work did not prove numerical
stationarity. Independent feasible predictions are still available. Retained
alternatives use `Unidentifiable`; no convergence/fit status asserts existence.
The wrong-profile controls, absent-signal controls and fully free spot-axis/cone
validation failure are reported separately from positive prediction cases.

The automatic shadow integration fixture detects five possible edges with no
oracle binding. Both intensity-only and automatic fits have zero XYZ error on
that easy scene, so it demonstrates integration but no measured localization
gain. It uses 383,608 fitting/validation rays. The broader S09 CPU regression
tests provide the separate correspondence/localization experiments.

| Original compiler BSP / native S11 | Train/validation/held-out records | Held-out interval MSE / black MSE | Relative source-Q L2 error |
|---|---|---|---|
| `point_room`, correct inverse square | 18 / 15 / 15 | `0 / 2495.889` | .0484% |
| `two_colour`, correct inverse square | 18 / 15 / 15 | `.222869 / 1170.602` | 5.16%, 5.86% |
| `door_occlusion`, correct inverse square | 18 / 15 / 15 | `0 / 2495.889` | .0484% |
| Same three BSPs, wrong linear family | Same geometries/targets and bounds | No model passes independent block gates | Unavailable, not zero error |

The compiler table uses encoded RGB8 intervals, not scene-linear/physical energy.
All six controlled runs repeat identically, including numerical/work results.
Their process time/RSS are kept separately in `compiled-resources.json`.
Across both repetitions, native compiler-fixture runs took .308–4.324 seconds
and 4,956,160–5,623,808 bytes peak process working set. These figures include
sanitized archive loading, footprint reconstruction and preparation.
In particular, `.222869` MSE is about .472 RGB8 RMSE; the nonzero amplitude
allocation error remains visible. These fixtures supply candidate XYZ; this is
not measured source-discovery accuracy or Raven compiler recovery.

The existing identity/order-independent source metrics report precision/recall
and geometry/colour errors per controlled case, with unknown physical units and
unidentifiable count explicitly separated. `metrics.csv` and `numerical.json`
retain diagnostic/rejected cases as well as successes. The original evaluation
suite also audited all 11 analytical answer files and 12 compiled datasets; that
audit is **not** a claim that S11 fitted all twelve compiler recipes. The native
joint compiler fit is scoped to the three rows above.

During development, failed checks exposed: MSVC 2019 rejection of a combined
default-member initializer declaration; a zero-volume placement box; an empty
direction baseline preventing bootstrap; a benchmark API key mismatch; and
rank/convergence diagnostics inherited from a parent with a different parameter
count. These were corrected and rerun. The independent spot validation rejection
was retained rather than weakening its gate. Final rank is bounded by the actual
parameter count, and parent convergence cannot certify a structural child.

Machine-readable numerical/source metrics, exact commands, resource measurements,
failures and unsupported checks are under `tools/rend2/joint/results/`.
Reproduction commands are in that directory's parent README.

The controlled set includes differently/same-coloured sources, mixed point/spot/
area, adjacent panels, tubes, weak+bright lights, coincident count ambiguity,
unknown scale, point/frame/dimension refinement, missing initial candidates,
passive coloured bounce, opaque partitions/openings, high-rho tails, unknown
materials/geometry, local+sun, clipped RGB and distinct grid/deluxe operators.
Automatic shadow association is used without oracle bindings; its simple joint
ablation already localizes exactly from RGB, so extra XYZ improvement is **not**
claimed there. Existing S09 oracle/automatic and compiler-boundary regressions
remain independent tests.

One deliberately free spot axis/cone configuration can fit training yet fail
the unchanged independent block gate. It remains rejected. The known-cone axis
ablation is separate; an excellent training fit does not make the more flexible
model physically successful. A compiled RGB interval fit likewise does not prove
unique Q: clipping, finite bins and correlated sources leave feasible sets.

Remaining scope limits: no certified stock compiler identity, general empirical
compiler bounce replay, ordered compressed-grid GI, arbitrary vertex-light
fitting, complete curved/alpha/translucent transport, higher bounce fitting or
automatic runtime publication. Source recall is measured only against controlled
private answers. Unsupported observation combinations remain unavailable and
cannot win by dropping required validation coverage.
