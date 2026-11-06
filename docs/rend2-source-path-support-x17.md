# X17 — declared bake-scene direct paths and frozen receiver coverage

X17 repairs an actual production CPU orchestration error. Direct source factors
now query the **same declared bake scene used for placement**, rather than first
passing through a passive-GI operator's conservative runtime-motion envelope.
The eight original maps still produce **no accepted nonempty source model**.
More mandatory direct predictions are supported; the remaining unknowns still
fail the complete-coverage gate. This is not original JA lamp recovery.

The original V02 cohort, BSPs, captures, splits, source work budgets and acceptance
thresholds are preserved. A fresh paired baseline uses the saved native executable
from parent `d3ef7c59a`; this separates X17 from X16's intervening source-provenance
changes. The V02/57-map historical reports are not overwritten. Full numerical
receipts are in [results/v1](../tools/rend2/path_support/results/v1/comparison.json).

## End-to-end contract

For each declared profile/scenario and fixed receiver manifest:

```text
source kind / actual occupied and emitting support
  → SourceSupport + PointRefine placement in declared bake scene
  → original receiver + declared historical compiler push (normals unchanged)
  → same static BVH + selected/unknown bake occupancy interpretation
  → production Bake/Shape factors (with source extent and angular law)
  → pre-encoding direct response → declared observation encoding
  → all frozen mandatory training rows
  → independent validation, then final held-out only after acceptance/freeze
```

The loss remains the existing robust, area/block-normalized distance to each
encoded quantization/censoring interval. Contributions are combined before the
profile's output encoding. There is no subtraction or renderer lighting change.

The former `Joint::TransferVisibility` called `Transfer::QueryPath`, then
`PlacementProvider::ConstrainVisibility`. A transfer containing *any* unknown
runtime sweep had already changed nonblocked rays to Unknown; the occupancy layer
correctly refused to clear that upstream uncertainty. Thus an independently
declared conditional bake scene could admit a source centre but not evaluate its
otherwise supported direct rays.

`Joint::SceneVisibility` now calls `PlacementProvider::Query` once, using the
placement provider's pinned static BVH and bake-state records. It charges the
same global ray/traversal limits. This does **not** clear a GI Unknown. Passive
Transfer links, reciprocity, energy accounting and `Transfer::QueryPath` retain
their existing uncertainty and runtime-envelope policy. Unknown GI remains an
explicit limitation of the stock direct-only branch.

Before installing the direct evaluator, Joint checks that every mover retained
by Transfer also exists, with the same model identity/captured support, in the
declared placement interpretation. Missing/stale occupancy is InvalidInput,
not implicit transparent geometry. Placement/static/transport map and geometry
generations must agree. Open/closed alternatives use independent requests/keys;
they are never concatenated into one physical scene.

## What each path outcome means

| Evidence | Numeric direct response | Physical claim |
|---|---|---|
| Known opaque static obstruction | Supported zero | This declared direct path is blocked |
| Verified selected bake-state obstruction | Supported zero | Blocked in that independently specified scene |
| Verified open/excluded model and supported static ray | Supported response | Valid for that declared state |
| ConditionalBake, ray disjoint from captured model support | Supported historical recipe may return an Assumed response; strict physical-integral policy remains unavailable | Counterfactual static-bake hypothesis, PolicyAssumption/ConditionalBakePath; no measured historical pose or runtime guarantee |
| Ray intersects unspecified captured support | Unavailable, BakeOccupancyUnknown | Bounds do not tell whether a door was open/closed or opaque |
| Unbounded possible model geometry | Unavailable, UnboundedOccupancy | A local source-centre assumption cannot clear the entire ray |
| Unknown alpha/translucency, missing geometry, patch/trace ambiguity | Unavailable with original optical/geometric bits | Uncertainty is preserved |
| Unsupported receiver formula/style/normal class | Excluded by frozen source-independent manifest | Unsupported implementation scope, not absence of light |
| Source in known solid / unrelated solid penetration | Source hypothesis rejected | Numerical RGB fitting is not attempted |
| Cancellation/work/memory failure | Typed hard error | Not black and not completed work |

Ray/support intersection now uses a bounded segment–AABB slab test rather than
intersection of the segment's enclosing box with a model box. A diagonal ray can
miss a box even when those enclosing boxes overlap. This only narrows *spatial
relevance*: disjoint unspecified bake models remain explicitly Assumed, and
captured bounds are never claimed to be full runtime sweeps or filled solids.
If the existing static query applies a normal bias, both its guarded offset
segment and effective ray enter the occupancy support test. A miss of the
unshifted segment alone cannot clear a model intersecting the shifted ray.
No new historical receiver offset or universal clearance epsilon is introduced.

The new cause bits are separate from `VisibilityState`/`RayReliability`:
`BakeOccupancyUnknown` (bit 15), `UnboundedOccupancy` (16),
`ConditionalBakePath` (17). An assumption flag is not a visibility certificate.
Unknown opacity or an upstream static Unknown cannot become Visible through
these changes. A known static blocker still terminates the path as blocked.

## Receiver and source geometry remain independent predicates

X15's geometry-only common-domain preflight and immutable IDs/folds remain in
force. Source XYZ, RGB, fit residuals and final targets do not choose exclusions.
Both conditional falloff profiles are compared on the same declared receiver
domain. Runtime source-specific path support is a separate gate: **every admitted
mandatory row must receive an available prediction**. Failure on even one row
cannot make a candidate a lower-error winner.

A compiler-pushed receiver can be classified free by the original BSP domain yet
encounter a visual triangle at the ray origin. That remains a source-dependent
`OriginOnBoundary` trace uncertainty; it is not repaired by replacing shading
normals, dropping the sample or inventing a new historical push. The pinned
compiler-tree replay remains a separate explicitly selected scope. Joint's
current historical Shape adapter uses generic static visual rays; this patch
does not assert Raven trace equivalence or add a new compiler formula.

X14 source support is unchanged. Mounted/finite emitters still use their actual
occupied support, original emitting faces, outward side and independent receiver
rays. This task adds no vacuum-sphere exemption, fake mounting surface, broad
rectangle around a fixture union, or finite-source-to-centre-ray reduction.
Unsupported mounting/assemblies and known penetration remain rejected.
The physical finite-source integral continues to require its stricter supported
visibility: an Assumed path is not promoted to an exact integral merely because
the historical operator permits conditional numerical predictions. This is an
explicit remaining source/operator policy limit, not a hidden receiver dropout.

## Stock progression on the frozen eight-map cohort

Both profile branches run actual production Hidden/S05/SceneSchedule/Joint.
The following ranges are **available / mandatory coarse rows in source-bearing
attempts**, including repeated waves. They are not fraction of map area, source
recall or a completed fit. A dash means source geometry rejected all tested
activation proposals before forward fitting.

| Map | Inverse-square before → after | Linear before → after | Remaining stop |
|---|---|---|---|
| hoth3 | 62/64 → 63/64 | 60/64 → 63/64 | One visual origin-boundary path; other candidates Clearance |
| hoth2 | 51–52/64 → 54/64 | 46–52/64 → 46–54/64 | Unspecified bake occupancy, optical and boundary/patch uncertainty; some Solid/Clearance |
| vjun2 | — → — | — → — | Clearance |
| kor1 | 90–95/103 → 92–95/103 | 90–95/103 → 94–95/103 | Bake occupancy and visual origin boundary |
| yavin1b | 44–60/64 → 48–63/64 | 44–58/64 → 48–60/64 | Unknown optics, patch approximation, some bake occupancy |
| t1_danger | — → — | — → — | Proposed centres Solid |
| mp/duel4 | 55/64 → 55/64 | 60/64 → 60/64 | PatchApproximation on 9 / 4 required rows; other candidates Clearance |
| mp/ffa3 | — → — | — → — | Clearance and UnknownGeometry |

For example, hoth3 inverse-square source `3629271412735231837` now has 63/64
available rows and 60 positively illuminated observed rows with predictions.
Its missing sample `1035399556294990417` retains `OriginOnBoundary`,
`PolicyAssumption`, `ConditionalBakePath`. The source is **rejected**, not promoted
because 63 rows look useful. All bounded missing IDs and raw/typed cause bits
are retained in the machine-readable source-attempt records.

Source-independent supported mapped observation area is unchanged:

| Map | Supported mapped chart area | Original selected → common rows, all folds | Positive common training rows |
|---|---:|---:|---:|
| hoth3 | 52.99% | 878 → 528 | 213 |
| hoth2 | 56.05% | 519 → 343 | 142 |
| vjun2 | 61.59% | 702 → 496 | 199 |
| kor1 | 59.39% | 855 → 593 | 268 |
| yavin1b | 51.17% | 569 → 249 | 121 |
| t1_danger | 3.76% | 415 → 307 | 189 |
| mp/duel4 | 62.74% | 278 → 232 | 94 |
| mp/ffa3 | 72.12% | 852 → 682 | 299 |

These are mapped chart/receiver areas, not full world area, free-space volume,
disjoint GI area or numerical prediction coverage. Unknown-position/padding
records have no invented area. For t1_danger, 628,577 mapped rows covering
175,212,537.62 WU² are `DistinctNormals`; only 12,654 rows / 7,107,976.43 WU²
are supported. No normal substitution or unverified compiler interpolation is
introduced to inflate its very low 3.76% support.

Availability is not positive source prediction. The bounded first-amplitude
factor capture contains 2,135 receipts: **22 positive, 1,667 explicit model zeros,
446 unavailable**. These include patch/observation and coarse/strict preparation
receipts, not independent receiver votes. hoth3's first source responses are all
zero even on its positively illuminated observed rows. Other positive receipts
occur in hoth2, kor1, yavin1b and mp/duel4. A model zero can come from a known
blocker, angular support or the declared intensity-dependent influence cutoff;
it is not proof of a dark original map. The new diagnostics retain that distinction.

Original V02: eight maps reached Joint, zero accepted nonempty models.
Paired X17: eight maps still reach Joint, four calls per profile, 64 total;
source-bearing mandatory coverage improves on four maps. No source-bearing
attempt passes the complete model/coverage/validation requirements. Final
held-out evaluations, uncertainty/publication of a winning stock model and
canonical nonempty stock assets remain unavailable. Held-out MSE is **null**,
not zero. Completing independently evaluated no-source baselines is counted
separately from physical-source reconstruction.

Stock identity remains UnknownProfile. The two NetRadiant inverse-square/linear
recipes, gamma/primaries/material policies and compiler-relative Q scale are
conditional hypotheses. MP captures retain their existing SP material
interpretation limitation. No original XYZ/type/count accuracy, absolute SI
photometry, sun absence or full-GI correctness is inferred.

## Receipts, cache identity and bounds

Joint attempts now retain typed failure codes, complete successful/failed training
coverage receipts and supported positive *observation* counts. Placement failures
have no borrowed coverage from another attempt. Existing missing-ID/issue arrays
remain bounded (128 per attempt). Score availability is not inferred from those
diagnostic arrays.

The existing optional V03 `DiagnosticSink` captures actual direct-ray queries and
source/sample factor responses, even if the full model is rejected. Capture is
bounded to 4,096 unique ray geometries plus 4,096 factor receipts, with the existing
record/byte limits and explicit omissions. It retains the first response at each
factor geometry, not every RGB iterate. Targets are not supplied to the sink;
the sink cannot change source admission, confidence or optimization. Physical
Lux and compiler/physical-relative incident response domains are explicitly
tagged. Licensed spatial exports stay in private run directories; only their
hashes and aggregate diagnostics are committed.

PointRefine advances to algorithm **4**, Joint to **8**. Direct block/root keys
explicitly bind the occupancy policy/scenario; the effective observation-policy
key also binds the placement policy. Existing Producer fingerprints include
Joint version and complete placement input identity. Old scene checkpoints/
dependent numerical caches cannot masquerade as the new semantics. Canonical
asset wire schemas and renderer-facing resources are unchanged.

The scheduler keeps its existing limits: four waves/profile, two newly active
catalog candidates/wave, 12 models/Joint call, 8 active sources maximum,
12,000 evaluations, 400,000 fitting rays, 80M fitting work/call, 90 s call safety;
scene caps 4M rays/500M work/384 MiB/240 s, outer process 900 s/1 GiB. These
are offline deterministic/safety budgets, independent of Forward+, shadow maps,
froxel capacities, exposure or PBR consumer settings. Search is incomplete;
unsearched/deferred source candidates do not establish absent lamps.

## Controlled validation and regressions

The extended native X13/X17 witness covers positive and blocked paths under the
same receiver/operator, verified open/closed states, unspecified and unbounded
occupancy, diagonal bounding-box false overlap, static opaque walls, unknown
alpha and missing visual geometry, changed-scenario factor-cache rejection,
solid placement, stale generations, cancellation and work/memory errors.

It reproduces the old scope error with a real Transfer containing a distant
unknown mover. `Transfer::QueryPath` correctly stays Unknown in its original
scope; the independently declared conditional bake path supports positive
historical forward predictions. Actual Joint recovers the supplied controlled
lamp amplitude and independently predicts held-out observations, while retaining
Candidate state, `occupancyConditional`, `MovingPath` and no physical-existence
certificate. This is supplied geometry, not blind source discovery.

Final numerical/resource/test receipts and exact commands are recorded alongside
the [benchmark runner](../tools/rend2/path_support/README.md). The existing fixed-XYZ
compiled controls, wrong-profile negatives, contribution queries, serialization,
cache/lifecycle tests and SP/MP builds are reported separately from stock discovery.
Shader history drift is retained; the X17-versus-parent disabled shader comparison
must have zero differences without rewriting any historical baseline.

The final native controlled witness passes **60 checks**, twice, with identical
semantic receipts: 461 forward calls, held-out interval MSE 0, 0.609/0.610 s,
5,419,008 peak process bytes in both runs. It uses supplied source geometry and
withheld amplitudes. The production compiled supplied-XYZ controls give:

| Owned compiled scene | Held-out interval MSE | Black MSE | Wrong-profile source result |
|---|---:|---:|---|
| point_room | 0 | 2495.8889 | DiagnosticOnly, final error unavailable |
| two_colour | 0.22286924 | 1170.6019 | DiagnosticOnly, final error unavailable |
| door_occlusion | 0 | 2495.8889 | DiagnosticOnly, final error unavailable |

All published control sources remain Candidate; zero are Resolved. The repeated
two-colour archive is byte-identical (SHA-256
`a478ab37b5a7d189376fd118d4b03e822dfd0ca8b6fde9c554696c19f099155d`).
This is amplitude/forward/publication validation, not blind XYZ/type discovery.

| Stock map | Local jobs/profile | Jobs proposing candidates/profile | Before → after process s | After peak MiB | Fitting rays, both profiles |
|---|---:|---:|---:|---:|---:|
| hoth3 | 697 | 146 | 45.250 → 33.547 | 114.84 | 104 |
| hoth2 | 573 | 15 | 18.047 → 17.921 | 83.14 | 396 |
| vjun2 | 1,896 | 306 | 57.282 → 56.125 | 113.76 | 0 |
| kor1 | 374 | 38 | 26.188 → 25.985 | 96.77 | 960 |
| yavin1b | 156 | 17 | 12.407 → 10.641 | 48.89 | 616 |
| t1_danger | 81 | 20 | 61.282 → 46.563 | 57.32 | 0 |
| mp/duel4 | 20 | 4 | 8.953 → 5.438 | 35.43 | 180 |
| mp/ffa3 | 968 | 370 | 55.422 → 23.547 | 78.83 | 0 |

These wall times include archive scan, geometry/topology/transfer preparation,
discovery, fitting and bounded diagnostic export. Builds/tests ran concurrently;
the differences do not establish an isolated speedup. The 2,256 fitting rays
and 66 source forward calls are separate from local discovery and GI-build rays,
which are retained per map in JSON. Work-limited sparse GI is still incomplete.
All 32 paired input/domain/coverage checks and the final source-hash check pass.
Final hoth3/mp/duel4 repeat has
identical profile reports, source/model IDs, path causes, ray/work counts and
receiver support; resource times are excluded from determinism comparisons.

Final complete CPU suite: **45/45 pass**, 232.83 s. Python report/controller/
protocol tests: **26/26 pass**. SP/MP Rend2 Release DLL builds pass. All 27 disabled
GPU programs / 51 stages compile on Intel UHD OpenGL 3.2; preprocessed shader and
binding comparison against parent `d3ef7c59a` has **zero differences**. Comparison
against historical baseline `380ee0f7` still fails with 7,278 diff lines; that
historical drift is explicitly retained and no baseline is rewritten. No game
executable is launched.

Exact additional build/regression commands (the stock/control commands are in
the runner README):

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
$py = 'C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2
$env:MSBUILDDISABLENODEREUSE = '1'
$env:CL = '/MP2'
cmake --build build/x16-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 1 -- /nr:false
$env:PYTHONPATH = 'C:/Users/Keyten/Desktop/projects/OpenJK-rend2/tools/rend2/datasets'
& $py -X utf8 -m unittest tools.rend2.path_support.test_reports tools.rend2.discovery_x16.test_contract tools.rend2.producer.test_controller tools.rend2.validation.test_report tools.rend2.receiver_support.test_reports tools.rend2.stock_joint_fits.test_reports
& $py -X utf8 tools/rend2/occupancy/test_protocol.py
& $py -X utf8 tools/rend2/lightrecon_regression.py --cpp C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/gcc.exe --no-gpu --output build/x17-disabled
& $py -X utf8 tools/rend2/lightrecon_regression.py --cpp C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/gcc.exe --output build/x17-disabled-gpu
& $py -X utf8 tools/rend2/producer/compare_revision.py --revision d3ef7c59a --gpu-report build/x17-disabled-gpu/report.json --out build/x17-disabled-delta
git diff --check
```

Environment: Windows 10.0.19045, 12 logical CPUs (Intel Family 6 Model 165),
GCC 16.2 C++11 CPU build; MSVC 2019 x64 Release / SDK 10.0.17763 renderers.
Run protocols pin the native executables, source files, captures and BSP hashes.
Initial setup attempts (missing old capture path, module import invocation,
missing shell cmake PATH) were corrected without changing scientific policy.
Available intermediate logs are hashed. An initial biased-ray test was tangent
and therefore never activated the static API's normal offset; the corrected
non-tangent positive/unknown controls both require a positive applied bias. Its
initial failed fixture receipt is retained separately from the passing final
tests. Earlier eight-map passes are retained privately; the final pass includes
quantity-tagged diagnostics, early metadata validation and effective biased-ray
support, with identical stock lighting/coverage results. Existing unrelated S09
documentation edits are not part of X17.

## Remaining physical limits

The next obstacles are supported source placement/mounting evidence, genuinely
uncertain visual/bake-state paths, unsupported historical receiver/trace classes,
low map-area support, bounded source discovery/selection and missing full passive
transport. They require independent geometry/compiler/material evidence or
additional conditional alternatives, not relaxed coverage or Unknown→Visible.
No game was launched; no recovered emitter, sun, field, fog or shadow consumer
was enabled. Gameplay image equivalence is not claimed from numerical tests;
the disabled shader/build regressions are recorded as their own evidence.
