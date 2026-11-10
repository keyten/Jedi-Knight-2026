# ASR23 — blind multi-method benchmark and ablations

Executed 2026-11-10 (Australia/Sydney), base revision `12c7497c6`, active branch
`rend2-unified-wip`. This checkout's recorded remote is `keyten/BetterJK`; it
does not identify itself as `keyten/Jedi-Knight-2026`. No remote was changed.

The frozen suite provides evidence that combining original grid and lightmap
proposals improves conditional source discovery over strict JA proposals. It
also **fails full all-cues acceptance**. It does not establish that every added
method helps, that finite shapes or spotlights are solved, or that the mixed
bridge is recovered. The successful blind multisource witness belongs to
grid-only and adaptive-off; the all-cues version adds a false duplicate.

The historical solver, canonical types/codec/storage, renderer, world shading
and runtime consumers are unchanged. All fitted lights retain `Candidate`
status; none is certified as an exact historical lamp. The only pre-existing
tool change accepts five explicitly named owned fixture shaders in the
controlled-material manifest, still requiring exact BSP hash, surface ordinal
and original shader reference. It does not allow arbitrary stock assumptions.

## Frozen protocol and evidence

[Protocol](../tools/rend2/blind_benchmark/results/v1/protocol.json),
[numerical policy](../tools/rend2/blind_benchmark/policy-v1.json),
[complete results](../tools/rend2/blind_benchmark/results/v1/results.json),
[tables](../tools/rend2/blind_benchmark/results/v1/tables.md),
[selected parameters](../tools/rend2/blind_benchmark/results/v1/selected-models.json),
[integrity audit](../tools/rend2/blind_benchmark/results/v1/integrity.json),
[repeat](../tools/rend2/blind_benchmark/results/v1/repeat.json), and
[replay instructions](../tools/rend2/blind_benchmark/README.md) are checked in.
Detailed public inputs, private evaluator files and complete training paths are
preserved locally at `build/asr23-frozen-v5` and `build/asr23-final-{a,b}`.

Sixteen owned compiled scenes cover direct point, spot, finite emitting
rectangle, thin-strip tube approximation, two colors, overlapping white sources,
sun/sky, skylight, one-bounce color transport and its direct control, doorway
occlusion, sealed-wall leakage, empty and mixed sun/local, plus both preserved
V02 multiroom bridge variants. Twelve reuse original locked compiler BSPs;
empty and mixed sun/local are fresh compiler executions; the two bridges reuse
their original BSPs and original partitions. No scene is a CPU-generated field
described as a compiler bake. The grid consists of original JA records and
indirection, never generated probes.

Before any solver invocation, the author/controller pins answer, original BSP,
geometry, public/evaluator input, source and binary SHA-256 values. It publishes
training RGB, original-like grid training records, original shader-derived
fixture faces, geometry and geometry-only evaluation receiver coordinates in
separate public directories. Light entities, source XYZ/color/power answers and
validation/held-out RGB are not supplied to the solver process. Source positions
used in scoring are opened only by the trusted evaluator after every training
model path is frozen. Its validation selections are written and hashed before
held-out scoring. This is a process/API boundary, **not an OS sandbox** against
a malicious optimizer with arbitrary filesystem access.

The small scenes use seed `20261110`, 256-WU blocks, origin `(17,31,47)`, and a
1-WU gap plus three original footprint radii of erosion. A geometry-only
farthest-point schedule retains at most 64 rows per fold: normally 64 training,
10 validation and 64 held-out. The spot has 55/10/53. The original bridge folds
retain 64/34/56. There are 2,094 total retained receiver rows. Original grid
records are split by byte-identity hash, with only their training assignment
published. Lightmap held-out predictions therefore condition on allowed grid
evidence; this is not an entirely unseen spatial volume across every modality.
The small validation folds limit generalization strength.

The empty bake collapses to a solid black lightmap and degenerate receiver UVs.
It supplies twelve correlated training-only geometric queries for an abstention
control. Independent held-out illumination is **Unavailable**. Missing
geometry/target support is not replaced by a black prediction.

All nine configurations share the same native modern transfer evaluation,
training appearance fitter, validation selector, sample schedule and count
penalties. `ja_baseline` is the unchanged strict JA Hidden proposal method with
this common fitter. This isolates proposal/evidence differences; it is **not**
an end-to-end accuracy claim for every historical JA fitting component.
Hidden/adaptive search uses training surface contrast and captured shadow paths;
the final native columns use source support, occupancy, visibility and sky
queries. No separate shadow-estimator ablation is claimed.

Grid proposals reuse bounded angular fitting of original encoded directions,
grouped by relative code color. Grid brightness is never regressed against
lightmap brightness. Native modern relative transfer columns are compared under
an explicit same-material RGB8 **forward appearance** hypothesis with fixed
gamma=1, compensate=1, exposure=0. Sources sum before coupled peak compression;
RGB8 quantization/censoring intervals are scored. The new benchmark-only fitter
does not invert encoded bytes into calibrated irradiance, identify the stock
bake law, or assign SI meaning to fitted power. Passive-bounce scenes deliberately
test this direct model's misspecification.

The tube case is an original emitting rectangular prism represented by its
downward face. It is a tube **approximation**, not an analytic cylinder bake or
proof of a historical tube. The bridge contains three local point/spot roots
**and** a finite emitting prism, so its full positional denominator is four.
The suite has 21 local roots total; solar alternatives are counted separately.

## Measured comparisons

| Configuration | Proposal recall / precision | Selected recall / precision | False local sources | Mean held-out MSE / black MSE |
|---|---:|---:|---:|---:|
| Strict JA baseline | .381 / .075 | .333 / .350 | 13 | .47591 |
| Fixture only | .095 / 1.000 | 0 / Unavailable | 0 | .81319 |
| Grid only | .810 / .486 | .524 / .917 | 1 | .34579 |
| Lightmap only, adaptive | .381 / .082 | .381 / .500 | 8 | .49951 |
| Grid + lightmap | .857 / .135 | .667 / .778 | 4 | .30338 |
| All cues | .857 / .133 | .667 / .737 | 5 | .26265 |
| Sun off | .857 / .133 | .667 / .778 | 4 | .30338 |
| Area off | .857 / .133 | .667 / .737 | 5 | .26265 |
| Adaptive off | .857 / .126 | .667 / .737 | 5 | .28505 |

All cues matches 14/21 roots, versus 7/21 for strict JA proposals. Its mean
normalized held-out appearance error is 44.8% lower. Error aggregation is the
arithmetic mean of per-scene ratios, excluding unavailable or zero-energy
denominators; it is not an irradiance error or statistical significance claim.
Precision/recall use one-to-one positional matching, and type correctness is
reported separately. All 14 all-cues position matches have the expected type.

Grid+lightmap recovers both true roots in the overlapping-white scene where
grid-only selects one virtual aggregate position, but it also adds a false
third source. Sun handling improves mixed sun/local error from .34691 to .10744
and sun/sky from .84727 to .47580, while introducing additional false local
sources in sun/sky. Fifteen complete solar alternatives were evaluated across
three scenes. Lower error does not certify solar direction/count correctness.

The area ablation produces identical selected results: no finite fixture
survives the shared training/validation count cost. The native panel and tube
proposals are geometrically admissible, but this run does not demonstrate
finite-source selection. Adaptive-on has lower average error than adaptive-off,
yet loses the clean two-color success and introduces a bounce-related duplicate.
These ablations do not support an unconditional claim that each cue improves
source discovery.

## True blind multisource witness and mixed-bridge failure

On `two_colour`, grid-only and adaptive-off recover two **proposed**, rather than
supplied, point positions. Their errors from the private compiled answers are
12.13 and 6.08 WU. Both selected types are correct, source recall and precision
are 1.0, there are no geometry violations or false sources, and held-out MSE is
45.20 versus black MSE 2667.22: ratio .01694. The 64 held-out rows cover three
spatial blocks; worst-block RMSE is 8.30 RGB code units. That last value is
reported, not hidden behind the passing ratio gate.

All cues on the same scene selects three local sources, matches the two real
roots and has ratio .05264. Its false duplicate makes its clean multisource gate
**Fail** despite a visually/photometrically plausible model.

| Preserved bridge | Strict JA matches | All-cues matches | All-cues held-out / black MSE | Status |
|---|---:|---:|---:|---|
| Direct | 0/4 | 2/4 | .12113 | Fail |
| Passive bounce 1 | 0/4 | 2/4 | .07771 | Fail |

Both all-cues bridge models find the two point roots and miss the third local
spotlight and finite emitting prism. Even the bounce variant's ratio below .10
cannot pass the source-recovery gate. The original bridge fold's limited
spotlight validation support remains a constraint; it was not repartitioned
after seeing the answer. This tests the previously failed three-light bridge
without describing partial recovery as a solved scene.

## Numerical gates, costs and verification

| Gate | Definition | Result |
|---|---|---|
| Suite blind multisource | At least one scene with >=2 true roots; all one-to-one matches <=32 WU; every type correct; no false sources; held-out/black MSE <=.10; no geometry violation | **Pass**, two-color with grid-only/adaptive-off |
| All-cues blind multisource | Same gate, all-cues configuration | **Fail**, no clean full scene |
| Recall improvement | All-cues micro recall strictly above strict baseline | **Pass**, .667 > .333 |
| Held-out improvement | Lower mean scene-normalized error | **Pass**, .26265 < .47591 |
| False selected local sources | All-cues total <=0 | **Fail**, 5 |
| Empty/sky false local sources | Total <=0 in empty, skylight, sun/sky | **Fail**, 2 in sun/sky |
| Geometry | Zero selected non-Free/non-SkyBoundary placements | **Pass**, 0; unknown alternatives excluded |
| Wall leaks | Maximum prediction <=4 RGB8 on held-out dark sealed-wall receivers | **Pass**, 1.0285 on 17 rows |
| Stock source truth | Independently known stock positions | **Unavailable** |

The work policy is fixed: 128 search nodes, 50,000 rays, 2,304 forward calls and
64 million traversal work per strict/adaptive search; eight retained proposals
per provider, 48 combined candidates, four selected sources, 192 model fits and
160 coordinate sweeps per fit. Grid search uses at most 192 independent records
and 512 pairs. Every fit/source/search cap passed. The maximum observed per-scene
method fit count is 55. Metered fit work counts RGB row-channel line trials;
the closed cap is `192 * 160 * (4+1) * 4 * training_rows * 3`.

The first final replay takes 117.92 seconds, its repeat 113.86 seconds on the
recorded Windows/Python 3.12/NumPy 2.3.5 environment. Shared native proposal/basis
stages total 1,191,948 rays and 30,469,948 metered traversal work. All-cues fitting
takes 10.05 seconds, 440 total fits and 31,863,276 metered RGB row-channel trials.
Native provider/basis work is reused across configurations; these timings are
not independent full-pipeline timings per method. Equal caps do not imply equal
spent work, and no wall-clock early-stop selection is used.

Both final runs match exactly after excluding only wall-time fields: selected
parameters, training paths, predictions and work counters are identical for all
sixteen scenes. The separate audit checks spatial IDs/blocks, public geometry
fields, record-hash training assignments and budgets. It compares 854 original
grid records across twelve scenes against the pre-existing native capture reader:
byte identities, styles and geometry classifications agree, with direction
agreement within 1e-6. Poisoning held-out RGB or moving private source answers
10,000 WU leaves every validation selection and training-model hash unchanged.
Imported proposal/geometry implementations also match the frozen base revision.

Seven protocol tests pass; nine focused native regression tests pass, including
discovery, strict/adaptive Hidden, sparse/aesthetic fit, shape selection, baked
forward, visibility and grid. Both modified/new tool translation units pass
C++11 `-Wall -Wextra -Werror -pedantic` syntax checks. Historical shared-core and
canonical type hashes still match ASR22. No renderer/GPU validation is claimed
for an offline-only change.

The initial frozen run is retained separately under `results/initial`. After
its held-out report, inspection found adapter defects: solar alternatives were
deduplicated by position, dead zero-cosine solar terms were traced, and freshly
compiled material facts were missing. The final freeze fixes those code/input
defects without changing numerical seeds, source penalties or acceptance gates.
The initial and final experiments have distinct input/code/binary hashes. Their
publication makes the post-evaluation correction visible; this is not a wholly
untouched external challenge set or a claim of threshold tuning on fresh data.

Remaining limitations include incomplete spotlight/finite-shape recovery, false
duplicate and indirect/solar explanations, the fixed modern direct appearance
model, a small validation fold, grid/lightmap cross-modality spatial correlation,
bounded nonexhaustive proposal search, no independent shadow-specific ablation,
and missing true stock source positions. Every stock position/precision/recall
claim remains **Unavailable**, regardless of visual quality.
