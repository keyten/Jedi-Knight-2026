# Inverse-lighting evaluation and uncertainty contract

2026-10-27. Initial repository baseline `b34ff011f`, `rend2-unified-wip`.
Metric schema `org.openjk.lighting-metrics.v1`. This is a trusted **evaluation**
framework, not a solver, renderer consumer, dataset-readiness certificate or
statistically calibrated source detector.

Implementation and exact commands: [evaluation tools](../tools/rend2/evaluation/README.md).
The executable produces JSON, flattened CSV and a ten-section Markdown dashboard.
Its self-contained controller fixture and adversarial numerical tests are separate
from original observation extraction. Existing raw/decoded observations, archives,
geometry queries and physical calibration are not reimplemented or modified.

## 1. Two layers, no aggregate percentage

**Dataset readiness** measures available evidence and its support. **Reconstruction
accuracy** measures a frozen model's physical claims and held-out predictions.
Neither substitutes for the other. Passing archive validation, publishing a valid
empty asset or extracting a thousand glow candidates is not lighting recovery.

The dashboard has independent sections:

1. Input completeness.
2. Geometric visibility confidence.
3. Photometric comparability.
4. Profile identification.
5. Source reconstruction accuracy.
6. Prediction accuracy and prediction coverage.
7. Direct/indirect attribution.
8. Shadow accuracy.
9. Uncertainty calibration.
10. Performance and deterministic reproducibility.

Every scalar metric has `status`, `value` and, where relevant, units, numerator,
denominator or exclusion reason. `Unavailable` means required evidence/evaluation
was not supplied; `NotComparable` means incompatible domains or absent truth make
the requested comparison undefined. Both have JSON null values. Empty denominators
are null, never perfect precision, zero residual or successful empty reconstruction.
No global score trades false lamps against a smaller RGB error.

The separate [numerical optimization benchmark](rend2-optimization-benchmark.md)
uses this source-matching/quality boundary while comparing independent amplitude,
profile, geometry, visibility, model-selection and direct/indirect subproblems.
Its numerical stationarity is not converted into a reconstruction-success claim.

## 2. Existing data and limits

Reuse these sources, with their hashes and original caveats:

| Evidence | Use in this framework | Claims excluded |
|---|---|---|
| 11 analytical scenes, `offline_fixtures.cpp` and separate answers | Point/spot/panel/tube/sun geometry, explicitly authored reference photometry, source permutation and matching controls. | Copying an answer to an oracle prediction is a metric sanity assertion, not source recovery. Analytical bounce/tube quadrature is not exact q3map2 replay. |
| 12 compiled q3map2 BSP exports and X02 views | Readiness census; frozen held-out encoded RGB zero baseline and target-copy assertion; point/spot authored parameter metadata. | Compiler intensity is legacy units, not cd/nits/lux. The six-face prism for panel/tube is not one analytical rectangle/tube. No new bake/forward-source fitting is executed. |
| Stock X02 `hoth3`, `hoth2`, `mp/ffa3` streams | Full-page count and known-footprint-area census, geographic/material/style distributions, unknown-domain reporting. | No stock source position, compiler identity, SI intensity or physical-source recall truth. Existing region graphs cover saved ROIs, not the whole world. |
| X03 profile witness | Validation-ranked conditional hypotheses, regret/nuisance sensitivity and preserved UnknownProfile. | No ranking-to-posterior conversion or Raven production compiler identification. |
| X04 35-texel boundary CSV | Dedicated 105-channel diagnostic with original old prediction and source-epsilon replay. | No global ignore band; no universal trace epsilon; no new instrumented compiler run. |

The [X02 report](rend2-observation-coverage-x02.md) supersedes the original two-page
prefix statistics: all 38/12/26 colour pages have been derived locally for these
three maps. This changes evidence coverage, not the unknown bake profile or source
ground truth. The new runner consumes these existing streams without starting JA.

The recorded `skylight` and `wall_leak` held-out folds are exactly satisfied by a
zero encoded prediction. They are retained as negative information-content
findings, not labelled useful localization benchmarks. A future frozen benchmark
must add spatially independent exposed/shadowed receivers before claiming source
localization on those configurations; never select replacement samples after
inspecting a final solver's held-out error.

Related contracts: [offline format](rend2-offline-observation-datasets.md),
[decoder](rend2-observation-decoding.md), [PBR units](rend2-physical-pbr.md),
[localization](rend2-light-localization-research.md),
[bake identifiability](rend2-bake-identifiability-x03.md),
[shadow footprints](rend2-shadow-footprints-x04.md),
[GI attribution](rend2-inverse-gi-research.md),
[static visibility](rend2-static-visibility.md) and
[regions](rend2-lighting-regions.md).

## 3. Readiness: area, count, support and missing evidence

For a unique receiver observation i with covered surface area a_i and quality
predicate q_i in {true,false,unknown}, report:

```text
known-footprint area A = sum_i a_i over positive known footprints
covered area(q)       = sum_i a_i [q_i=true]
classified area(q)    = sum_i a_i [q_i!=unknown]
quality coverage(q)   = covered area(q) / A
unknown quality area = A - classified area(q)
```

Also report sample counts, unknown-area sample counts, duplicate counts and all
exclusive mapping categories. A full-world fraction needs an independently known
eligible receiver-area denominator. If it is missing, report NotComparable.
**Padding has no invented world area.** Area sums over available mappings are not
the surface area of an entire map. Approximate footprints carry separate quality
predicates; they cannot silently become exact areas.

`readiness()` streams tri-state predicates for raw RGB, world mapping, normal,
footprint, comparable source colour, material/style, visibility, region, grid
direction, deluxe direction, censoring, unsupported and ambiguous data. Counts and
known-area fractions coexist. Qualities overlap; do not sum their percentages.
The X02 adapter uses existing taxonomy rather than new pixel encodings. In
particular:

- Unknown transfer or primaries cannot produce comparable source-linear RGB.
- Unknown material response is unknown, not today's PBR albedo.
- No deluxe observation is absent evidence, not a zero direction.
- Valid mapped positions need not have region IDs.
- A locally complete receiver box is not a certified source-to-receiver ray.
  The general per-source visibility readiness predicate remains unknown until an
  actual query/hypothesis is supplied. Scene completeness is reported separately.
- Grid record presence, quantized direction and free-space certification remain
  separate. Deduplicated logical indices are not independent measurements.

Report by map, page, surface type, material/style, region sides, 256-unit spatial
bins, height and encoded brightness bins. Unmapped samples stay in an explicit
unmapped bin. These brightness bins are encoded-domain distributions, not physical
luminance quantiles. Indoor/outdoor labels require explicit scene evidence; they
are not inferred from region IDs or height.

Deduplicate aliases using map hash, original logical page, texel, surface/style/
channel and mapping interpretation version. Distinguish a duplicate observation
from two legitimately independent measurements. The new generic reducer rejects
duplicate fidelity target IDs and counts repeated readiness IDs once; X02's
existing first-encoding-per-page policy handles alternative image representations.
Before calling it on arbitrary new datasets, the controller must canonicalize
different IDs referring to the same footprint. Identical RGB alone is not a key.

Grid coverage should use cell volume where that is a meaningful sampling measure,
plus logical/unique-record counts; it is not receiver area. Streams have separate
weights and errors. Do not aggregate volume, area and count in one RMSE.

## 4. Information content and observability

Without a source hypothesis, report cautious geometric proxies: distinct receiver
surfaces, position bounds/baseline, area-weighted normal second moment

```text
M = sum_i a_i n_i n_i^T / sum_i a_i
```

and its three eigenvalues. One planar receiver has rank-one M regardless of pixel
count. This is not a Fisher matrix: differing visibility, distance, saturation and
operator semantics still determine source identifiability. More nonzero eigenvalues
are useful evidence diversity, not proof of unique sources.

With fixed candidate geometry, use the nonnegative intensity response matrix A.
With continuous parameters theta, use a declared local Jacobian J. The provided
`information(rows,scales,cutoff,noise_whitened)` computes singular values through a
small symmetric Gram eigensolve:

```text
B = W^(1/2) J S      # or fixed intensity response A with the same scaling policy
singular values = sqrt(eigenvalues(B^T B))
rank = count(sigma > relative_cutoff * sigma_max and numerical_floor)
condition = sigma_max / sigma_min, or explicitly infinite/rank deficient
```

Supply dimensionless parameter scales S (metres, angle, intensity scales recorded
in manifest) and W; do not compare a raw mixed-unit Jacobian's condition numbers.
The implementation has 32 columns / 100,000 rows limits and is intended for
bounded diagnostic subsets. Gram conditioning loses precision relative to a full
SVD; inspect ill-conditioned cases with a higher-precision/reference implementation
before claiming a numerical rank boundary.

Noise-whitened J plus an explicit valid likelihood may justify a *local* covariance
approximation. This reducer does not manufacture one. Unknown scale, co-located
sources, a single plane, clipping and visibility plateaus can retain genuine null
directions. Nonlinear profile likelihood/feasible sets and disconnected hypotheses
must survive even when one local Jacobian is full rank.

Additional source-specific metadata required by benchmark manifests: receiver
distance range, independent shadow/caster configurations, cross-region ray outcomes,
near-wall distances and directional evidence. Unknown metrics remain unavailable
until supplied by a forward/visibility evaluator. PVS and common region membership
are not substitutes.

## 5. Physical-source matching

### 5.1 Identity-independent assignment

Use resolved physical point, spot, rectangle, finite tube and sun records. Sky and
transport belong to separate accounting; neither increases physical-emitter TP.
Candidates, observations and rejected hypotheses are not recovered sources.
Known truth IDs are evaluator-only labels. Predicted IDs/order never enter matching.

Create a bipartite edge if source type agrees and all required known geometry is
within the frozen gates. Costs are the mean of normalized available geometric
errors, bounded by one. Run **maximum-cardinality, then minimum-cost assignment**
with Hungarian matching and explicit dummy unmatched entries. The dummy penalty
is larger than the total possible gain from switching valid edges. A greedy
nearest-neighbour assignment is insufficient and has an adversarial test.

```text
TP = assigned pairs
FP = predicted resolved physical count - TP
FN = supported truth physical count - TP
precision = TP/(TP+FP); recall = TP/(TP+FN); F1 = 2TP/(2TP+FP+FN)
```

Run a separate type-agnostic geometric assignment for type confusion. A nearby
spot instead of a point can be a localized wrong-type hypothesis while remaining
a missed/false physical-source detection in the strict table. Do not count
transport/directional reflections as lamps to improve recall.

Split/merge diagnostics report one-truth→many-prediction and many-truth→one-prediction
geometric neighbourhoods. They do not grant several TP for one source or erase FP.
These neighbourhoods are diagnostic, not verified energy attribution. Equivalent
co-located configurations must also be evaluated as alternative complete source
sets, with explicit non-identifiability. Their count error is visible, but cannot
by itself establish that data could distinguish the original authoring choice.
Tie assignments may permute equivalent sources; aggregate metrics must remain
invariant. Any per-source claim in a tied group needs an ambiguity label.

### 5.2 Geometry, frame and units

Positions and full dimensions use original world units; metres = world/
`worldUnitsPerMeter`. Report vector XYZ differences and Euclidean error in both.
Sun has no finite position. Spot/rectangle direction means **emission propagation**;
sun direction means **receiver→source**; tube axis is unoriented. Mismatched
conventions are NotComparable, not silently negated. Angular error is acos of the
normalized dot product. Zero/nonfinite directions are errors.

Rectangle normal and in-plane roll are distinct. Rectangles admit 180-degree edge
axis symmetry and swapped width/height with 90-degree tangent rotation; a square
has that symmetry without renaming dimensions. Tube length and radius are not
interchangeable; reversing its longitudinal axis is equivalent. Spot half-cone
angles are compared in degrees. Effective emission size versus geometric source
size should be separate fields in future extended fixtures.

Near-wall clearance is measured relative to known mounting/recess geometry. A
certified source inside solid is an explicit plausibility violation. Unknown
geometry is unknown. A source on a fixture plane is not automatically invalid:
the permitted emitting side, dimensional extent and geometry error envelope matter.
Disconnected regions alone do not prove an impossible source room.

### 5.3 Scene-specific gates

Freeze gates before testing final solver settings. Choose tolerances from source
extent, receiver footprint, geometry accuracy, directional quantization, source
visibility and calibration, not one XYZ constant for every scene. Recommended
manifest records:

```text
position gate = documented localization resolution from receiver/shadow evidence
direction gate = encoding + geometry/edge measurement envelope
dimension gate = fixture geometry uncertainty, not a fixed percentage of map size
photometric budget = calibration + encoding + model discrepancy in declared domain
```

The bundled metric fixture uses an **illustrative 8-world-unit** position gate
(half a 16-unit compiled luxel step), 5-degree axis/roll, 3-degree cone and 10%
dimension gates. These are frozen numerical assertion settings, not acceptance
thresholds for stock maps or all emitters. Photometry is reported after geometric
matching, not used to rescue a poor location or reject unknown-scale sources.

## 6. Photometry and colour

`photometry` records unit, calibration/relative domain, calibration kind, Y intensity,
linear colour and primaries. The evaluator preserves canonical distinctions:

- **PhysicallyAuthored:** SI error only with shared calibrated domain/unit.
- **LegacySceneEquivalent:** calibrated relative/presentation error; not proof of
  original candela or lux.
- **UncalibratedRelative:** relative error only within a shared declared domain.
- Unknown/incompatible scale: NotComparable intensity error; location/type can
  still be evaluated.

After matching, report `abs(Ip-It)/It` for positive comparable truth intensity and
absolute difference in its declared unit only where justified. Zero truth intensity
has no relative percentage. Chromaticity uses distance on the nonnegative normalized
RGB simplex `||rgbp/sum(rgbp)-rgbt/sum(rgbt)||2` with known common Rec709-linear
primaries. This is an explicitly named chromaticity metric, not perceptual Delta-E.
Black and unknown primaries have no defined chromaticity comparison.

The analytical fixture adapter converts the fixture's intensity×RGB convention
to canonical Y intensity, using its declared RGB and the known reference colour
policy. Rectangle/tube geometry follows the fixture evaluator, not a generic
answer axis that happens to be present. Compiler answers retain legacy compiler
units; a six-face surface emitter is not collapsed into a false analytic source.

Unknown global gain can be bounded on **training-only calibration constraints**:
intersect `target_low/prediction <= gain <= target_high/prediction` for positive
reference responses. Empty intersection is inconsistency; a wide set is ambiguity.
`scale_feasible_set` implements these deterministic bounds, **not a confidence
interval or physical calibration**. A statistical scene-scale CI needs a recorded
noise/model-discrepancy likelihood and independent-scene/cluster resampling or
profile likelihood. Do not fit gain using final held-out targets or normalize every
candidate to identical brightness. Report the effect of allowing gain separately.

## 7. Held-out predictions and incomplete coverage

Prediction identity uses stable observation keys, not source IDs. The controller
defines `comparison_id`, domain, primaries and interval targets. Predictions must
match all these; missing/incomparable records contribute to missing coverage and
never become black. The metric library does no gamma/overbright/calibration fitting.

For a target channel interval [lo,hi], with hi possibly +infinity:

```text
d(x,[lo,hi]) = max(lo-x, 0, x-hi)
RGB interval RMSE = sqrt(sum_i a_i sum_c d_ic^2 / (3 sum_i a_i))
```

Use original quantization intervals, transformed through a declared monotone
decoder/profile when comparing source-linear values. RGB8 nearest-rounding gives
half-byte intervals; saturated 255 is a lower bound. A prediction in EncodedRGB8
must already have the profile's encoding/clamping applied and lie in [0,255].
Do not compare unclipped linear intensity against encoded bytes. The bundled
compiled baseline is deliberately **encoded-domain**; it does not label unknown
historical RGB as linear.

Linear Rec709 luminance uses `(0.2126,0.7152,0.0722)` and the implied luminance
interval. There is no linear-luminance error for raw gamma/unknown encoded RGB.
Surface and grid errors are separate observation operators; grid ambient/directed
do not become indirect/direct truth. Directional error is reported separately with
quantization-bound excess, not treated as a verified lamp direction.

Mandatory companions to error:

- Supported observation area used for fitting / total supported target area.
- Comparable prediction area / all requested supported target area.
- Missing/incomparable counts and their geography.
- Disjoint supported areas classified as physical-resolved, approximate-indirect-only
  or unresolved; distinguish solver declarations from evaluator certification.
- Errors by region, lighting component/category and brightness range, plus raw
  census breakdowns. The category label must come from trusted metadata/truth.

A perfect prediction on 10% of supported area retains 10% coverage. Do not score
unsupported map regions as physical failures, but report their separate absence.
Compare quality at fixed coverage and coverage at fixed error budgets. Report
risk–coverage curves for uncertainty-based abstention; a scalar conditional RMSE
alone cannot rank a partial result above a broad one.

## 8. Shadows, visibility and physical attribution

Visibility has Visible/Blocked/Unknown confusion counts, certified-query accuracy,
unknown rate, and leaked predicted energy for known blocked paths. The geometry
adapter must use the existing CPU API; no PVS or same-region shortcut is permitted.
Unknown optical masks, moving doors and unbounded unknown blockers remain unknown.

Shadow records use evaluator-selected receiver/caster associations. Measure edge
displacement in world units and metres, penumbra width error, false association
rate, missing/extra edges and boundary interval coverage. For unassociated edge
sets, use bidirectional distance plus explicit missed/extra components and an
occluder association cost. The implemented scalar edge API measures supplied
correspondences; it is not an edge detector or automatic image matching algorithm.
Do not select only the closest correct edge after observing the answer.

Keep the 35 original q3map2 boundary texels as their own diagnostic with source
hash, 105 channels, maximum/signed residuals and area-weighted interval loss.
The previous 28-byte maximum remains visible. X04's source-epsilon replay and its
limitations are retained as a named baseline, not generalized into a universal
boundary fudge. No texel is dropped to improve a new score.

Shadow-localization improvement requires matched runs with identical sources,
priors, budget and observations: report position-error difference with/without
shadow evidence on independent receivers/casters. This framework can pair supplied
ablation records; it does not execute or invent optimization results.

Component truth, when known, separates direct local, direct sun, sky, reflected
transport, legacy ambient and signed model discrepancy. Report component energy
errors even if their sum matches observed RGB. Root/source contribution membership
detects duplicate references. A physically plausible reflected field is not the
same as unresolved residual; source recall remains separate. Root metadata catches
declared duplication only: missing/mislabelled attribution cannot be inferred from
RGB similarity and stays unverified.

## 9. Unknown bake profile evaluation

Compare only candidates using identical observation domain/partition, nuisance
parameter budget, source hypotheses and resource/regularization policy. Report
validation and held-out error, fitted nuisance values and unsupported features.
The explicit profile metric returns viable alternatives within a preregistered
validation-regret allowance, their separation and dependence on the allowance.
The X03 adapter also reports 0/.5/2-unit sensitivity; these are diagnostic regret
budgets in that experiment's domain, not posterior confidence thresholds.

Known controlled profile truth allows selected-family accuracy, truth retention
in the viable set, false singleton claims and wrong-attenuation rejection rate.
Stock data allow **conditional evidence only**, so profile recovery accuracy is
NotComparable. Number of viable profiles depends on the explored family: one
surviving candidate does not prove the candidate catalogue was complete.

Report features separately: attenuation may be distinguishable while bounce, sky
sampling, grid reduction or historical compiler revision remain unidentified.
Cross-profile spread is model uncertainty, not repeated independent measurements.
Nuisance/calibration/material freedom can erase apparent discrimination and must
be included in sensitivity tables. Preserve alternate complete configurations.

## 10. Uncertainty calibration, feasible sets and alternatives

Canonical `Estimate` bounds, physical-emitter confidence and transport confidence
are not automatically calibrated probabilities. A scalar [0,1] heuristic can rank
outputs, but must not receive a probability calibration score under a false label.

The evaluator scores explicit probability **claims** with a recorded calibration
protocol; that is a claim being tested, not proof it was already calibrated.
Existence labels come from gated source matching, including unmatched predictions
as negative outcomes. Ambiguous tied/exactly equivalent truth should have unknown
labels, not arbitrary certainty. Precision–recall/score sweeps and physical FP
counts remain necessary because a source detector can hide hard cases by abstaining.

```text
Brier = mean((p-y)^2)
ECE   = sum_bin N_bin/N * |mean(p)_bin - mean(y)_bin|
interval coverage = count(truth in reported set) / comparable truth count
```

Brier properly penalizes confident errors more than moderate uncertainty. ECE is
binning-dependent; always include counts/reliability tables. Bins are frozen before
final evaluation. Wilson intervals are emitted only when the caller explicitly
assumes independent trials; multiple emitters/luxels in the same scene generally
are not independent. Use scene/room/block resampling for correlated observations,
with enough independent held-out scenes for uncertainty estimates to be useful.
This initial fixture run does not establish deployment probability calibration.

Position uncertainty needs **joint** 3-D region coverage, not three independent
95% axis coverages mislabelled as 95% joint coverage. `joint_position_coverage`
supports ball sets; scalar interval coverage supports intensity, individual
coordinates and shadow boundaries. Report widths, nominal levels, method, sample
size, conditional matched coverage and unresolved fraction over *all supported
truth*. Do not hide missed detections by reporting only intervals of easy matches.
Categorical type probabilities require a normalized vector; report multiclass
Brier and top-type accuracy separately from strict source detection.

For exact ambiguities, publish disconnected feasible source sets/count alternatives,
not a tiny Gaussian around one prior-selected answer. Evaluate true-configuration
retention, overconfident singleton rate and interval widths under:

- Co-located lights / one versus many.
- Single-plane or narrow-distance receivers.
- Saturated/weak/sparse evidence.
- Unknown scale, profile, material or optical blocker.
- Subsampling and profile/material changes.

Never multiply heuristic confidences to create a posterior. Position, intensity,
profile, visibility, albedo and absolute scale errors are correlated. Coverage
studies must vary those uncertainties jointly or state which are fixed. Reserve
independent calibration/validation scenes; final held-out scenes never select
confidence scaling, model complexity or bin boundaries.

## 11. Blind access boundary

The existing X02 publisher creates physically separate `inputs/` and `evaluator/`
files. `TrainingView` contains only training RGB, sanitized geometry and training
grid records. It has no held-out backing buffer, raw lightmap images, vertex RGB,
private answer handle or helper path to hidden targets. The trusted evaluator owns
answers, full archives and withheld streams. The new strict audit checks the input
artifact allowlist, metadata allowlist, map/role/schema, member hashes and streaming
limits. Controller prediction ingestion rejects any attempt to override truth,
matching policy or map identity through prediction JSON.

Negative tests attempt held-out reads through TrainingView, wrong-role/cross-map
construction, early EvaluationView held-out access, injected answer files, private
compiler metadata, truth overwrite and near-neighbour filtered-texel leakage.
Existing X02 tests cover shared grid-record and alternative-image leakage. Real
compiled public geometry is checked with `lightrecon_dataset_quality public-check`;
a Python filename audit alone cannot prove binary sanitization.

**Python objects and a subprocess working directory are not an OS sandbox.** A
hostile executable with unrestricted filesystem access can open a sibling private
path regardless of the API. Mount only `inputs/` read-only in an external sandbox/
container or use a separate OS account/machine with no access to evaluator files.
Do not mount this repository's examples, recipes, full archives or evaluation
output. This change implements/tests the artifact/API boundary; it does not install
a platform-specific adversarial-process sandbox or claim one was tested.

Recipe/compiler truth is withheld in deployment-like UnknownProfile trials. A
known-profile controlled experiment may explicitly publish that profile as an
allowed input; report it as a different protocol. Exact source answers and hidden
light entities are never permitted priors. Geometry and genuine fixture hints can
remain where they would be available in deployment, with source photometry removed.

Freeze model, output bytes and manifest hash before final held-out evaluation.
Validation may be opened by the controller at recorded checkpoints. A final report
is evaluator-only; feeding it back into optimization consumes the holdout and
requires a new untouched benchmark for subsequent final claims.

## 12. Baselines, ablations and regression policy

| Baseline | Question answered | Current execution |
|---|---|---|
| Zero / declared ambient | Does prediction improve over absence/simple fill? | Encoded zero on real compiled held-out targets; no inferred ambient baseline. |
| Original grid directional approximation | Does rich reconstruction improve legacy approximation? | Unavailable until a profile-correct separate operator is supplied. |
| Physical-source oracle | Best-case forward-model error with known sources. | Source-matching sanity; no new complete physical forward rendering. |
| Target-copy oracle | Are metric plumbing/domain/identity correct? | Exact prediction sanity only; explicitly not a solver baseline. |
| Fixed candidates | Effect of geometry priors with bounded intensity solve. | Existing X03 witness, not a new fit. |
| Incorrect profile | Exposes model discrepancy misattributed as lights/GI. | Existing X03 conditional alternatives. |
| Many weak emitters | RGB fit can preserve false/split sources. | Deterministic adversarial prediction with perfect supplied RGB. |
| Indirect-only | Flexible GI can suppress true lamps. | Deterministic adversarial prediction and attribution error. |
| Full constrained model | Future solver target. | Unavailable; not implemented. |

Pair ablations with/without lightgrid, material hints, visibility, shadows, indirect
field and emissive priors. `ablation_pairs` accepts external completed runs and
requires identical dataset/split/policy hashes, seed and resource budget, with
exactly one feature changed. Report source detection, held-out error, coverage,
leaks, attribution, uncertainty and cost jointly. No ablation results are fabricated
where no solver ran.

Regression policy is a separate frozen JSON containing metric paths, preferred
direction and allowed change. Missing/noncomparable required metrics fail the
regression check rather than become zero. Keep hard invariants (no wall leak on
exact opaque fixtures, no answer leakage, finite results, no fake source-success
claim) separate from statistical/perceptual budgets.

Numeric budgets derive from observation quantization, compiler verification scope,
receiver/edge resolution and calibrated error envelopes. Freeze them on development
scenes, not after inspecting final answers. Compare paired scene/block bootstrap
differences and uncertainty; a tiny change below numerical/model error is not an
improvement. Prefer Pareto comparisons at fixed supported coverage. Do not accept
lower RGB error at the expense of significantly worse physical FP, recall, wall
leaks, coverage or calibration unless explicitly defining a different product goal.

## 13. Reproducibility and bounded execution

The controller manifest pins case/answer and prediction file hashes, BSP content,
compiler executable/recipe where known, decoder/units/calibration, geometry and
mapping versions, partition hashes, solver seed/settings, metric implementation
version/source hashes, thresholds and resource limits. Preserve failed and
unsupported scenarios. Do not replace missing provenance with a guessed value.

The executable evaluates supplied predictions, not source fitting. Record actual
optimization wall time, peak process RAM, traces, iterations/function evaluations
and serialized asset size from the solver harness. No optimizer means those metrics
are unavailable. Evaluation timing is separate. Use existing `datasets/measure.py`
for actual Windows process peak working set; never label STL-accounted bytes RSS.
Semantic JSON/CSV is deterministic; wall time/RSS are separate measurement files.

The readiness adapter checks source-declared row counts against the actual unique
stream count and flags duplicates. `all_pages_declared_complete` records metadata
as received; `complete_derived_stream` additionally requires consistent observed
counts. A truncated valid-JSONL stream cannot inherit a completeness claim merely
from its source metadata. Original archive integrity/geometry validation remains
the existing `.jkobs` tool's responsibility.

Assignment is bounded to 512 sources; diagnostic Gram matrices to 32 parameters
and 100,000 rows; census to four million records. The census streams
records but retains IDs and grouped counters, so it is **not constant memory**.
Large archives should be reduced by deterministic shards with cross-shard identity
deduplication and merged sufficient statistics. The supplied runner handles current
map sizes; its measured RAM is reported, not guessed.

## 14. Validation and known limitations

The tool README and compact [results](../tools/rend2/evaluation/results/summary.json)
record exact commands, measurements and assertions executed for this change.
The generated dashboard provides the ten requested layers; the compact tracked
report excludes proprietary geometry, texels and source datasets.

Final run: 39 new metric tests, six existing blind-boundary tests, 11 X03 tests,
17 existing CPU CTest targets and 12 compiled public-geometry archive checks
passed. Six diagnostic examples, 11 analytical answers, 12 compiled datasets,
three full-page stock censuses and the complete 35-texel diagnostic were evaluated.
The evaluator took 176.587 seconds with 193,421,312 bytes peak OS working set;
these are not optimizer performance figures. Repeated example JSON/CSV matched
byte for byte. See [validation record](../tools/rend2/evaluation/results/validation.json).

The framework supplies metric primitives and controlled baseline wiring. It does
not supply a production solver, automatic shadow-edge detector, arbitrary binary
asset-to-Python binding, calibrated probability model, universal threshold, hostile
process sandbox, complete source-level GI attribution or source ground truth for
stock JA maps. Those absent quantities remain explicit. Passing the compact
fixtures is necessary plumbing validation, not general stock-map reconstruction
readiness.
