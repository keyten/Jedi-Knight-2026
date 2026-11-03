# X15 — frozen common receiver support before source fitting

The production CPU solver now freezes a **geometry-only comparison domain** before
model proposals. Unsupported receiver classes are reported as explicit exclusions;
candidate-dependent prediction failures remain in the admitted denominator and
cannot improve a candidate's score. No source equation, original normal, lightmap,
renderer consumer or runtime lighting path is changed.

## Two questions, two reports

1. Compare profile/source hypotheses on the same declared common receiver domain.
2. Measure how much of the original captured observation area each operator can
   support. Low error on a small admitted subset is not whole-map readiness.

`ReceiverSupport::Manifest` is an immutable, versioned object built from a bounded
`ChunkSource<Geometry>`. That interface contains receiver geometry, stable IDs,
spatial roles, material availability and channel presence, **no measured RGB,
emitter XYZ or source amplitudes**. A trusted controller may prepare the manifest
across training/validation/held-out geometry. Fitting receives the manifest and
training values; the held-out colour streams remain with the existing controller.

Each manifest records map, geometry, point-domain, partition and policy identity;
profile/operator keys, requested numerical domain, source feature classes,
compiler-point versus physical-integral mode, trace/quadrature policy, calibration
and assumptions; sample/surface/material/region/block IDs; required channels and
spatial roles; actual receiver area; common admission, per-profile reasons and
uncertainty. It preserves unknown material/optics counts. An absent or zero area
does not become a representative stratum weight or physical transport patch area.
Unknown absolute scale is never converted into candela/lux by this preflight.

Support is the intersection over the *declared operator hypotheses*, computed
before source fitting. Each operator's own supported count/area is retained.
Identical receiver capability for inverse-square and linear attenuation does not
identify a stock compiler; the value kernels still differ and can only be ranked
with supported independent evidence. `UnknownProfile` is not a falloff law.

## Receiver capability and compiler-origin scope

Historical checks reuse the actual production `Bake::Capability`, with typed
`CapabilityReason` values. They validate profile formulas and feature metadata,
evaluation/trace/output mode, static style, mapped planar placement, numerical
mapping bounds and geometric/shading normal relationship. Material reflectance
is required only when the declared observation operator uses it. Bounded
material hypotheses stay conditional; unknown bake albedo is not current PBR
base colour. Lightgrid ambient/directed/direction and deluxe remain separate
operators. Joint's supported grid subset is unpacked accumulation, not global
ordered grid compression/merging or a definition of indirect/direct light.

Distinct shading normals remain unsupported for the scoped historical
interpolation/push model when `|ng-ns| > 1e-5`. The original normal is preserved.
This task adds no patch interpolation, normal replacement, compiler nudge,
universal sample offset, gamma correction or historical Raven rule.

For the historical operator, the fixed compiler receiver position is:

```text
surface/deluxe: xcompiler = original_world_position + receiverPush * geometric_normal
unpacked grid: xcompiler = original_world_position
```

The point-domain classifier uses the same zero tolerance as the existing
historical forward ray. A known solid origin, boundary origin or unknown origin
has a typed preflight exclusion. These cannot be repaired by moving the original
sample, copying the shading normal, or selecting a favourable source. Missing
verified compiler placement/nudge behaviour is a model limitation, not proof
that the historical receiver or light was impossible. Source paths are still
independently evaluated. Physical finite quadrature has a separate geometry
contract and source-dependent ray offsets; compiler point sampling is not
silently replaced by that integral.

Some stock mapping records have `LuxelBasisMismatch` against the original BSP
basis while the adapter explicitly uses independently mapped UV positions. The
full-map report preserves those raw flags separately. Its numerical capability
percentage is **conditional on the adapter's UV/placement hypothesis**, not the
stricter raw `ReceiverFromLuxel` factory or verified Raven correspondence.
Overlap, duplicate/mixed support, unsupported motion and other unavailable
geometry remain separate exclusions. The preflight does not certify optical
visibility merely because normals and formulas are supported.

## Frozen denominator and runtime gate

For profile set P and geometry-only receiver metadata gi, define

```text
Si,p = numerical implementation admits gi under profile/operator p
C = { i : Si,p is true for every p in the requested comparison }
```

The bounded coarse schedule Cc is selected from C by fixed spatial groups, not
candidate predictions. Every required channel/region/block must be represented.
A 64-row budget for 69 required groups fails in preflight, before a model attempt;
the stock controller retains the existing `max(64, training_group_count)` rule.
Final strict fitting uses the full frozen supported domain. Excluded observations
and their area stay in the original-input coverage report.

For each admitted i, interval/censored residuals still use the existing objective:

```text
r(y,[lo,hi]) = y-lo if y<lo; y-hi if y>hi; 0 otherwise
objective = channel/block-normalized, area/statistical-weighted robust loss
```

Neither quantized/censored targets nor direct-source equations are changed.
The runtime coverage audit requires a finite, available prediction for **every**
admitted sample in the fixed schedule. It distinguishes Available, KnownBlocked,
UnknownVisibility, UnsupportedSource, WorkLimit, MissingPrediction and NonFinite.
A known blocked contribution is a valid explicit zero. Missing data are not zero.
An incorrectly labelled blocked prediction containing positive light is rejected.

Joint skips only exclusions already present in the manifest. It reports their
IDs/reasons/area and never computes factors for those original receiver rows.
If a source cannot predict a mandatory admitted receiver, the score is unavailable
and the source/model cannot win. Candidate-specific source movement, extent,
visibility, GI support and numerical budgets remain runtime constraints. The
same observed set is used for zero-source and source-bearing models. A flexible
candidate cannot select its own easy receiver subset.

The supplied manifest also checks partition cardinality and receiver geometry.
Deleting rows and reducing the caller's `count` does not turn a truncated fold
into a successful comparison. Coarse subsets are selected internally only after
the complete training domain is pinned. Validation/final values do not choose
sampling density, IDs, exclusions, source locations or work budgets.

## Caching, diagnostics and integration

`Joint::Pin` prepares receiver support before discrete model attempts. Explicit
controller manifests are reused across scene waves and separate profile branches.
The manifest key enters Joint configuration and Producer dependency fingerprints;
scene resume/catalog identity therefore changes with support policy. Roles,
receiver geometry, normal relationship, style, footprint and domain changes are
not accepted as the same frozen input. Version changes are ReceiverSupport 2,
Joint 7, Producer 7, SceneSchedule 2. Canonical asset wire schema is unchanged.

Prepared historical factors retain the fact that receiver capability was checked
for their exact dependency key. Amplitude iterations reuse that certificate;
FAST envelopes, intensity-dependent support and coupled encoding are still
evaluated by the actual strict forward operator. The factor key also binds
assumption permissions, required features, units and grid/replay policies, so a
cached factor cannot bypass a newly required capability. A changed receiver or
policy returns InvalidState. Ray visibility is reused only for unchanged source
geometry and visibility dependencies.

Manifests are bounded and can be read from streaming geometry and written one
entry at a time. Defaults allow 100,000 rows, at most 16 operators, 64 MiB and
16M capability checks; requests have explicit cancellation and memory/work errors.
No dense source × all-luxel matrix is added. The full-map stock census aggregates
by exact metadata predicate classes rather than allocating a million-row source
matrix; receiver-origin probes remain spatial and are not cached by normal class.
The cache contains no observed brightness. Actual preparation/evaluation rays,
point-domain probes, checks, process time and RSS are separate counters.

Failed Joint attempts preserve the fixed admitted/used counts, missing IDs,
indirect issues and original ray issue bits. Producer assessment/provenance
records the frozen domain and incomplete map coverage. A signed residual is not
GI, and a converged scoped fit does not certify source existence or replacement.

## Controlled tests

The native suite tests equal/distinct normals, non-planar geometry, unknown style
and material, bounded reflectance, unknown/wrong profile, transfer uncertainty,
unpacked-grid support, common-domain intersection and per-profile coverage. It
also checks real production blocked-zero versus unknown visibility, two supplied
source geometries with different support, missing/truncated predictions, mutated
receiver/policy cache rejection, corrupt streams, cancellation, resource limits,
positive and dark validation, and the 69-group/64-row admission failure.

The production point-source fit recovers a supported positive observation set
with validation interval MSE 0 versus black MSE 396. These are known controlled
compiler-relative observations; this does not calibrate stock physical intensity.
An explicit 55/64 missing-prediction test cannot pass the coverage gate even
with perfect residual on the remaining 55 rows. Dark validation stays nonpositive.

## Stock evidence and before/after coverage

Frozen final results are in `tools/rend2/receiver_support/results/v4`. The maps
were selected before evaluation: `academy1` and `mp/duel4` run the production
scene scheduler/Joint; `hoth3` and `hoth2` receive full captured-map capability
censuses. Original archive/BSP/dataset hashes and previous X12 results are kept.
No authoritative stock emitter XYZ, source count or photometry exists.

| Map | Raw texels | Mapped samples | Supported historical receivers | Supported mapped observation area |
|---|---:|---:|---:|---:|
| academy1 | 393,216 | 235,581 | 140,833 | 77.78% |
| mp/duel4 | 32,768 | 19,842 | 9,477 | 62.74% |
| hoth3 | 622,592 | 403,084 | 200,359 | 52.99% |
| hoth2 | 196,608 | 79,565 | 38,858 | 56.05% |

The denominator is captured mapped receiver/chart area, including explicitly
unsupported classes. It is not full world surface area, disjoint transport area
or independent information coverage. Padding/unknown-position records have no
invented area. Both conditional NetRadiant falloff profiles have the same
receiver-origin push/normal restrictions, so their common support equals each
profile's own receiver support here. This is not profile identification.

The corresponding preselected comparison folds retain their original roles and
gaps. Counts below are original → common. No excluded colour enters an objective.

| Map | Training | Validation | Final held-out | Positive training before → after |
|---|---:|---:|---:|---:|
| academy1 | 474 → 301 | 145 → 95 | 159 → 94 | 474 → 301 |
| mp/duel4 | 113 → 95 | 93 → 78 | 72 → 59 | 103 → 94 |
| hoth3 | 412 → 225 | 239 → 155 | 227 → 148 | 320 → 213 |
| hoth2 | 189 → 142 | 157 → 94 | 173 → 107 | 189 → 142 |

The tabulated informative-positive count requires at least one channel in
(0,255); fully censored bright values are not included in that informative count,
although the existing censored objective still handles their positive lower
bounds. Training positivity is measured only after support freezes. Final held-out rows
in this table are available **geometry**, not claims that final predictions were
produced or that final target RGB was used for tuning. The MP repeat preserves
independent validation/held-out entry records exactly.

Exclusive full-map categories (counts; `excluded.csv` also gives areas):

| Map | Distinct normals | Non-planar | Other unavailable geometry | Fixed origin solid | Fixed origin boundary | Fixed origin unknown |
|---|---:|---:|---:|---:|---:|---:|
| academy1 | 37,974 | 0 | 7,138 | 42,619 | 7,013 | 4 |
| mp/duel4 | 0 | 6,524 | 616 | 863 | 2,362 | 0 |
| hoth3 | 71,959 | 32,208 | 4,404 | 59,685 | 34,443 | 26 |
| hoth2 | 3,783 | 19,534 | 1,626 | 10,491 | 5,272 | 1 |

These records had static style 0; no unsupported style area was observed in this
selected stock suite. Unknown style is covered by the controlled negative test.
`hoth3` provides substantial unsupported normal area (about 18.86% of mapped
area); a valid world-space colour does not make this interpolation formula
verified. Raw mapping issue-bit counts are additionally recorded and may overlap;
they must not be added as independent areas or interpreted as exclusive failures.

### Exact original failures

`academy1` sample **7804426648716081**, surface **308**, is found in the actual
original archive. It has `|ng-ns| = 6.01363088607e-5`, area
36.1249615352 world-unit². Its typed exclusion is `DistinctNormals`. The normal
is unchanged. The native controlled 69-group/64-row witness separately fails
before a model attempt; the stock schedule keeps its 69-row coarse budget. Its original 69 training
groups become 62 admitted groups after the explicit capability/origin exclusions;
the coarse stage preserves every admitted group.

The original `mp/duel4` source hypothesis `8246620835759861451` is reproduced by
the formula-only `v2` audit: 64 admitted coarse rows, 55 available, nine unknown
visibility predictions, zero missing-record substitutions. The exact nine stable
IDs and their final admission are preserved in `original-nine.json`:

| Original ray issue | Count | Origin-aware admission |
|---|---:|---|
| OriginInsideSolid | 4 | Excluded upfront: ReceiverOriginSolid |
| OriginOnBoundary | 3 | Excluded upfront: ReceiverOriginBoundary |
| PatchApproximation | 2 | Retained; candidate-specific path uncertainty |

The final solver then selects its fixed 64-row coarse schedule from the admitted
training set. It does **not** keep only the old easy 55. New promising candidate
paths encounter additional patch uncertainty: inverse-square has 55/64 available
(nine patch paths); linear has 60/64 (four patch paths). These are new schedules/
candidates, not an assertion that the same original nine persisted. Each fails
the coverage gate. No patch is marked transparent or exact to obtain a fit.

`academy1` likewise proceeds beyond the old unsupported-normal batch exception;
its remaining promising source attempts have only 21–33 of 69 required coarse
predictions available, with explicit runtime uncertainty. Both maps reach four
actual `Joint::Solve` calls **per profile**. Across those calls the recorded native
forward-call counts are 12 per profile for academy1 and two per profile for
mp/duel4. There is no accepted stock model, canonical recovered-source asset or
final held-out prediction. Source-bearing candidates are rejected; completing
no-source baselines is not successful positive-light reconstruction. `hoth3` and
`hoth2` are preflight censuses only in this task.

### Cost and determinism

| Map | Process wall seconds | Peak RSS MiB | Cached metadata classes | Spatial origin probes |
|---|---:|---:|---:|---:|
| academy1 | 18.562 | 45.24 | 4 | 190,469 |
| mp/duel4 | 4.641 | 27.51 | 4 | 12,702 |
| hoth3 | 18.828 | 89.79 | 6 | 294,513 |
| hoth2 | 6.437 | 56.21 | 5 | 54,622 |

Wall time includes input reading, existing hierarchical selection, geometry/BVH,
preflight and (for the first two maps) bounded scheduler fitting. These runs
shared CPU with builds; they are not isolated optimizer throughput benchmarks.
Origin probes are point classifications, not shadow rays or a count of internal
BSP nodes. Source/ray/work counters remain separately recorded in `stock.json`.
The native controlled witness is repeated with identical semantic output. The
MP full-census summary and independent fold geometry repeat exactly.

## Reproduction

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2
python -X utf8 tools/rend2/receiver_support/run.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/receiver_support/results/v4
python -X utf8 -m unittest tools.rend2.receiver_support.test_reports
$env:PYTHONPATH = 'C:/Users/Keyten/Desktop/projects/OpenJK-rend2/tools/rend2/datasets'
python -X utf8 -m unittest tools.rend2.datasets.test_blind_views tools.rend2.producer.test_controller
$env:PYTHONPATH = 'C:/Users/Keyten/Desktop/projects/OpenJK-rend2/tools/rend2/bake_model'
$env:JK_BAKE_FORWARD_EXE = 'C:/Users/Keyten/Desktop/projects/OpenJK-rend2/build/s06-gcc/lightrecon_bake_forward_tool.exe'
python -X utf8 -m unittest tools.rend2.bake_model.test_cpp_forward
python -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/s06-gcc/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/x15-compiler
python -X utf8 tools/rend2/producer/benchmark.py --tool build/s06-gcc/lightrecon_joint_tool.exe --out build/x15-producer-compiled
$env:CL = '/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

Use a new output directory for a reproduction; frozen runs are not overwritten.
The runner pins original 57-map inventory/BSP/dataset hashes, native/source
revision, settings, partition policy and environment before the selected run.
It repeats the controlled witness and the independent MP receiver geometry.
Licensed BSP/lightmap/coordinate payloads stay in the private dataset directory.
Versioned result JSON includes support manifests, valuable failed attempts and
resource diagnostics. The initial development census (`v1`) is explicitly marked
as such; `v2` preserves the nine original runtime failures before fixed-origin
admission. `v3` preserves the first origin-aware pass; `v4` also binds the explicit
compiler trace generation and validates context identity. Earlier runs are not silently overwritten by the final origin-aware policy.

## Limits

Common support certifies a numerical receiver class under declared assumptions,
not that all candidate paths or missing GI are known. In particular, alpha,
patch-caster approximation, unknown bake model states and unbounded geometry
remain uncertain. GI transport patch support is independent of receiver-fold
support; a missing passive model still requires an explicitly conditional
direct-only alternative. No approximate sunlight, grid fallback or physical
calibration is invented to enlarge the domain. Passing compact fixtures does not
establish general stock-map reconstruction readiness.


## Independent compiled-fixture regression

The existing pinned compiler suite was rerun against the production C++ tool:
**20 scoped direct cases pass**, 19 byte-exact and the established clipping case
within its existing one-byte tolerance. Each floor case contains 1,225 samples.
The scoped `multi_sun` replay remains exact on all 1,225 samples. Its difference
from the generic Python trace at former boundary samples is still visible
(`cpp_python_max=28`); X15 does not replace one trace model with the other.
Two bounce/bounce-grid controls remain **UnsupportedBounceReplay**, with maximum
six-byte discrepancy; they are not counted as successful bounce reconstruction.
The fixed wrong-profile RMSE is 14.4776, wrong attenuation with matching angular
law is 17.7202, and black is 24.9112 in the existing reference comparison.
No tolerance or reference data was changed.

The actual blind production Producer/Joint harness was also rerun, with supplied
candidate geometry and hidden amplitudes:

| Compiled scene | Correct-profile held-out interval MSE | Black MSE | Source RGB relative error | Wrong-profile outcome |
|---|---:|---:|---:|---|
| point_room | 0 | 2495.889 | 0.04836% | DiagnosticOnly, no accepted source fit |
| two_colour | 0.222869 | 1170.602 | 5.1619% / 5.8636% | DiagnosticOnly, no accepted source fit |
| door_occlusion | 0 | 2495.889 | 0.04836% | DiagnosticOnly, no accepted source fit |

Quantization/source allocation ambiguity remains explicit; low RGB error is not
proof of exact source parameters. The two-colour canonical archive repeats
byte-identically. All three sources/configurations remain Candidate, with zero
Resolved sources. This is fixed-geometry intensity fitting, not source discovery
or stock historical recovery. `compiler-comparison.json`, `compiled-blind.json`
and `compiled-resources.json` retain hashes and diagnostic/resource data.


## Final checks and one intermediate failure

The final complete CTest run passes **40/40**, including **55 X15 assertions**,
in 185.34 seconds under concurrent work. SP and MP Release builds pass. Python
blind/controller tests pass 11/11, receiver-report tests 5/5, and production
forward/compiled-boundary tests 6/6. No game process was launched.

An earlier full run had one failed repeated `P::Store` assertion among 40 cache
rewrites (`build/x15-ctest-console.log`, 39/40 tests). That assertion did not report
an I/O reason, so no specific filesystem cause is claimed. The test now prints
iteration/error/message on failure; its constraints and 40-write loop are
unchanged. The final full run passes the producer cache test (549 checks) without
that failure. This does not establish that the intermittent cache failure has
been fixed; its original log hash is retained in `checks.json`. No cache or
renderer workaround was added to conceal it.
