# ASR16: adaptive local hidden-source proposals

ASR16 adds a separately selectable offline strategy in
[`adaptive_hidden.cpp`](../tools/lightrecon_test/adaptive_hidden.cpp) with
[reproduction tools](../tools/rend2/adaptive_hidden/README.md). It narrows search
effort around plausible local illumination structures that lack convincing
visible fixture anchors. It proposes point/spot alternatives, not exact historical
lamps, source counts or calibrated photometry.

## Preserved strict path and scope

The inspected production `Hidden::Generate` begins with a bounded coarse lattice,
adds supported direction intersections, refines retained shape-score seeds and
checks domain/static geometry. Its opt-in X16 geometry-axis/point-spot refinement
policy already improves the previously studied V02 spotlight. ASR16 does not
change either implementation or their request identities. `hidden_candidates.h`,
`hidden_candidates.cpp`, canonical schemas/codecs and `scene_schedule.cpp` have
no changes.

The inspected scheduler also limits per-unit offers, alternates point/spot
tickets, round-robins shards and bounds the final catalog before capability
filtering. ASR16 leaves those limits intact and is not installed as a scheduler,
producer or renderer consumer. It currently returns tool-only proposal records;
future fitting/catalog integration must preserve the same admission and bounds.
No runtime lighting, world shading or double-lighting behavior changes.

The new executable compiles the existing training adapter with an explicit
adaptive-protocol flag. Strategy zero calls the original generator. Old protocol
output matches the original executable after removing wall-clock resource fields:
the checked parity case retains 16 candidates and 33792 rays. Native tests also
compare strict request identities, candidate counts and rays before/after an
adaptive call.

## Bounded volumes, refinement and evidence

An adaptive request must supply explicit local areas; there is no automatic
whole-map free-cell enumeration. Defaults allow at most 32 areas, 2048 WU extent
per axis, 512 input receivers, 64 scoring receivers per area, six coherent features,
32 directional constraints and 128 local batches globally. Each batch is an
eight-position, two-by-two-by-two search with the unchanged native scorer.

The source volumes combine:

- One eight-position geometry coverage fallback per eligible local area.
- Rays from supplied aggregate original-grid/deluxe directions, widened by their
  declared mixture half-angle. Unknown direction is not a source-position proof.
- Same-material, same-surface planar lightmap contrast features with at least six
  uncensored receivers, consistent normals/plane and positive contrast. A bounded
  local code-weighted centre seeds a receiver-normal ray volume. IDF-inspired
  support gates limit where this heuristic applies; there is no IDF source-depth
  estimate or claim that a spotlight obeys a point-light differential law.

Half the available pose tickets are reserved for refinement. The algorithm
shrinks promising searched cells and balances profile/channel/source-kind tickets.
At most four refinement tickets per round are used by the frozen benchmark.
Duplicate volumes are skipped. Scoring can stop before the cap when no new
supported child cell remains. This is a bounded local proposal heuristic, not an
exhaustive feasible-set search or globally optimal allocation.

ASR15 integration maps native areas to observation IDs and excludes an area only
when all of its mapped observations have sufficiently supported selected,
non-ambiguous fixture associations. Unknown/partial/ambiguous fixtures do not
suppress discovery. An unexcluded region means no convincing supplied anchor;
it does not certify the absence of real fixtures outside the observed pool.

Features are original encoded RGB8 observations. They can be remaining unanchored
structures after caller-side fixture/solar screening. ASR16 does not subtract
RGB8 fixture, sun or sky predictions or invent a calibrated residual irradiance
field. Unsupported global decomposition remains an uncertainty, not a zero term.
The evaluator compares encoded forward shapes per material, with two fixed probe
amplitudes and explicit profile alternatives. It fits no final intensity.

Dark and occluded receivers stay in that native paired shape comparison. A source
that illuminates measured dark samples or cannot illuminate known bright samples
loses shape compatibility. Unknown receiver rays retain unknown relations and
are excluded by the existing operator, never made transparent or black.
Adaptive retained locations require native `ProvenFree` feasibility and at least
four known visible supports. Partial unknown receiver support remains recorded.
Every adaptive source retains source-versus-transport ambiguity and unknown
existence confidence, even when the caller has a controlled direct-only recipe.

The audit records every visited volume, reason, original seed observation IDs,
node and candidate counts, dark controls and blocked relations. Standard proposal
records retain map/profile identity, original support IDs and relations, searched
cells and source-kind alternatives. Searched cells are resolution, not confidence
intervals. Point/spot labels are conditional numerical models, not inferred exact
historical source types.

Global ray/forward/traversal budgets are shared across batches, rather than reset
for each small request. A batch starts only with sufficient worst-case ray/call
allowance. Unexpected errors clear proposals. Accounted local storage reserves
space for the adaptive pool and the native batch scorer together. These are
algorithm/data bounds, not measured process-RSS reservations. Feature sorting is
bounded by input counts; its diagnostic work count is not a CPU-operation total.

## Frozen equal-cap study, 2026-11-09

[`results/v1/evaluation.json`](../tools/rend2/adaptive_hidden/results/v1/evaluation.json)
records all candidate counts, per-job work and failures. Each strategy receives
the same per-request caps: **128 poses, 50000 rays, 2304 forward calls and
64000000 traversal work**, with four retained alternatives per channel/profile/
kind. Position matching is fixed at 32 WU; the common encoded-shape gate is .12.
These are equal maximum budgets, not forced equal spent work. Feature processing
is additional bounded CPU work and is reported separately.

The three strategies are unchanged strict search, strict search with the existing
X16 geometry-axis/balancing options, and adaptive local volumes using those axis
alternatives. All receive identical source bounds, receivers and profile sets
within each case. V02 sums eight original geometry shards per variant.

| Case | Strict covered / sources; candidates | Strict geometry covered / sources; candidates | Adaptive covered / sources; candidates |
|---|---|---|---|
| Point floor census | 1/1; 16 | 1/1; 16 | 1/1; 16 |
| Oblique spotlight floor census | 1/1; 12 | 1/1; 16 | **0/1; 16** |
| Two-colour mixed-source floor census | 2/2; 46 | 1/2; 40 | **1/2; 35** |
| V02 direct | 2/3; 186 | 3/3; 178 | **3/3; 173** |
| V02 passive one-bounce mixture | 1/3; 207 | 2/3; 182 | **2/3; 183** |
| Compiled GI-only local region | no local primary; 27 | no local primary; 27 | no local primary; **23** |

Adaptive V02 direct nearest-position errors are 17.93, 24.23 and 23.78 WU.
The previously missed spotlight improves from strict's 74.34 WU to 23.78 WU,
including spot-axis alternatives with zero angular error in this owned vertical
spot control. Adaptive spends 38448 rays and 6196 forward calls across its eight
shards, versus strict's 92928 rays and 15888 calls. Existing strict-geometry
search also recalls all three, with 92928 rays and 14980 calls; its nearest spot
proposal is more accurate at 11.80 WU. This establishes a bounded-budget recall
improvement over strict and lower spent ray/call work relative to both controls;
it is not a uniform accuracy improvement.

The independent oblique spotlight is an explicit failure: adaptive's nearest
proposal is 48.04 WU away, outside the unchanged 32 WU gate, versus strict's
31.54 WU. The mixed-colour control also loses one source. Its normal-ray volume
and limited refinement can focus on a receiver footprint rather than an oblique
source or a weaker overlapping contribution. No source type or threshold is
changed to turn either miss into a pass. In the one-bounce V02 mixture, a point
source remains missed while the spotlight becomes proposed; indirect evidence
still perturbs direct-source localization.

For the negative control, a fresh owned JA room has a half-height opaque wall,
a warm primary outside the fixed right-room search volume and selected floor
receivers behind the wall. The pinned compiler runs with bounce=0 and bounce=1.
All 16 geometry-selected original receivers are black in the direct bake and
positive in the one-bounce bake (maximum code 20). Original codes are never
subtracted. The right-floor scope contains no authored primary. Adaptive's 23
low-cost alternatives are therefore **23 false proposals if interpreted as local
primary lights**, compared with strict's 27. Their false-positive fraction remains
100%; this is a retained counterexample, not successful GI/direct separation.
All remain ambiguous hypotheses with null source-existence confidence.

The small compiled cases use a new explicitly labelled full-floor discovery
census, with geometry-only farthest-point sampling. Their original historical
training folds have no floor support and are not silently rewritten or used for
new held-out predictive scores. V02 uses the original public request rows and
areas unchanged. This is a development replay of previously studied owned scenes,
not untouched held-out-scene generalization. No stock source truth is available.

All 60 native reports freeze before the separate evaluator opens source answers.
The proposal runner imports no answer evaluator, and native children receive only
sanitized geometry plus training requests. Frozen input/output/source hashes and
two complete byte-identical numerical runs are retained; timing fields are
excluded from repeat comparison. This is a procedural input boundary, not an OS
isolation claim.

## Validation and limits

The native suite passes 83 checks, including positive proposals, known-free
admission, unknown-domain refusal, cancellation, explicit bounds/memory/call
failure, fixture exclusion, source-direction seeding, dark evidence, deterministic
input reordering and unchanged strict results. Five Python tests check ASR15
anchor exclusions, equal caps/preserved rows, target-independent spatial sampling,
GI false-proposal denominators and runner/evaluator separation. Existing hidden,
scene scheduling, visibility, bake-profile/forward, ASR14 and ASR15 suites pass.

This stage has no unique-source, final fitting, historical-type, irradiance,
full-GI, stock-recovery or renderer-readiness claim. General oblique spotlight
initialization, mixed-source allocation and indirect-versus-primary discrimination
remain open. The frozen failures and ambiguous alternatives are part of the result.
