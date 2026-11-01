# X10: hierarchical observation sampling and independent evaluation

This changes observation scheduling and admission, **not the lighting solution**.
The previous 512-world-unit spatial/normal census selection can erase local
gradients. The new CPU sampler retains bounded same-material neighbourhoods and
separate independent evaluation geometry. No recovered light or renderer
consumer is enabled. Stock transfer, primaries and historical bake profiles
remain unknown. Admission under an explicit encoded-colour assumption is not
physical source-existence evidence.

The baseline is the all-stock adapter at `ec272791b` (X09). The working tree also
received the independent P04 photometric-boundary changes, ending at
`f0a61afab271a0a8835296b212185485ae0eea7b`; they are retained. The versioned
[witness manifest](../tools/rend2/observation_sampling/results/v1/manifest.json)
pins the actual source hashes, native executable hashes, BSP/dataset hashes,
partition/plan keys and repeated numerical results. Licensed stock geometry,
RGB and archives stay outside git.

## CPU contract and scheduling

`lightrecon/sampling.h/.cpp` implements two operations:

1. `Build(mapKey, partitionKey, geometry, policy, read, plan)` accepts geometry,
   immutable roles, material IDs and encoding availability. It has no RGB or
   dataset reader. It freezes validation and final-held-out selections.
2. `Refine(plan, trainingSignals, read, refined, optionalPriorities)` validates
   every signal against the training whitelist. It selects within-material
   extremes and neighbouring surface gradients from the bounded training pool.
   Optional training-residual, candidate-influence and supported-shadow ranks
   permit subsequent fitter-driven requests. They are bounded selection ranks,
   never confidence or objective weights. Invalid/hidden IDs are errors.

The seven purposes are distinct: exhaustive readiness census, coarse coverage,
candidate discovery, position refinement, possible boundary refinement, model
validation and final held-out evaluation. Census remains a streaming count;
its selected-row vector is intentionally empty. Position and boundary vectors
prepare future fitting work; this benchmark does not run position optimization.

Let `d` be the median actual receiver-footprint diameter in the supported
geometry pool. Initial correlation cells have side `1.5d`, local cells `8d`.
The coarse scale is the larger of the local scale and map receiver-baseline
length divided by the cube root of the coarse row allocation. These fixed,
dimensionless coefficients were not fitted to stock answers. They are a first
geometric policy, not universal room sizes. Footprint distributions can be
multimodal; a later per-neighbourhood scale estimator remains useful.

Material, quantized normal and correlation-cell coordinates define a correlation
proxy. One stable minimum-ID receiver represents each group. Opposite normals
remain separate. This is neither a test of physical surface equality nor proof
of statistical independence. It does not deduplicate physical transport area.
An attempted group bridging immutable roles is rejected; the sampler never
repairs that by reassignment. Conflicting metadata for the same ID are rejected.

Local jobs rank same-material geometric support and normal diversity, with
coarse-cell geographic diversity. The selector reserves up to eight receivers
for each of the two largest material groups, then distributes remaining rows
across material/surface/block/normal groups using geometric spacing. Signal
refinement ranks within-material extremes. Its nearest-neighbour edge proxy is
restricted to the same surface, material and training block. It is **not** an
automatic assertion of a cast shadow or a source-linear intensity gradient.

Stock inputs are the existing all-pages `.jkobs` observations. A streaming scan
builds a geometry-only, deterministic priority pool from 2-by-2 chart bins,
capped at 32,768 records; exact footprint clipping is performed only for retained
records. Unsupported/missing/ambiguous geometry keeps the existing taxonomy.
Pool overflow, deferred neighbourhoods and limited refinement are explicit.
The old scan and baseline output remain available unchanged.

## Admission and availability

Discovery requires at least six effective correlation groups, at least four
same-material samples, four uncensored positive samples and within-material
contrast at least 0.03. For channel `c` the diagnostic contrast is

```text
C = max_material,channel (max(training RGB_c) - min(training RGB_c)) /
                         (1 + max(training RGB_c))
```

Only uncensored channels contribute; at least four positive samples are
required for the corresponding material/channel. This is a channel-domain
admission proxy. Unknown stock bytes remain encoded bytes, not irradiance.
Constant colours on two different materials do not satisfy it. Saturated-only
and dark neighbourhoods are not admitted. A single plane can be admitted for
conditional discovery but retains insufficient-normal-diversity status; it
cannot certify XYZ localization.

Reasons remain separate: no informative positive signal, insufficient
same-material support, insufficient normals, missing independent folds,
unsupported geometry, unknown encoding, work limitation, correlated support and
saturation. An admitted training job may lack independent evaluation support.
That is not a validated source model. No admission failure means "no lamp".

For stock, **all selected jobs retain unknown encoding**. The adapter explicitly
opts into conditional encoded contrast and the hidden provider evaluates an
assumed profile. It does not identify a Raven compiler or create calibrated
physical RGB. Strict encoding policy rejects this admission.

## Folds, access boundary and weights

The stock split is unchanged: 256-WU blocks, seed `20261031`, origin `(17,31,47)`,
with the existing footprint/filter gap `3 * actual footprint radius + 1`.
Compiled tests reuse their original blind manifests, including seeds, origins,
blocks and gaps. None was reseeded to obtain positive observations.

Validation may come from neighbouring independent blocks, within an expansion
of the receiver bounds by `max(localCell, 3 * partitionBlock)`. A dense local
cell need not contain all folds. Map-wide geometry-only reserves preserve
broader evaluation coverage. This does not guarantee comparable source-distance
or visibility support; those limitations are reported rather than hidden.

`prepare_blind.py --sampling-geometry` optionally publishes a separate
`receivers-geometry.jsonl`, with an exact allowlist. It contains geometry and
roles, no RGB or emitter answers. `TrainingView.receiver_geometry()` checks its
hash, count, size and fields. It rejects injected target fields even with a
recomputed file hash. Original training members remain byte-identical.
The native `JKSAMPLE1` tool receives sanitized geometry and training RGB only.
The trusted controller opens answers and evaluator RGB **after native completion**.
It checks positive signal on the exact frozen evaluation IDs, not a convenient
replacement subset. Python's view is an API boundary, not an OS sandbox against
a deliberately malicious process that has independent filesystem permissions.

The trusted stock archive controller necessarily owns the raw dataset. It
passes no hidden RGB to `Sampling::Build/Refine`; hidden targets are read only
after selection for independent diagnostics. Stock audit is not falsely labelled
an OS-isolated adversarial solver process.

Five measures are deliberately distinct:

| Measure | Meaning/use |
|---|---|
| Actual receiver area `a_i` | Geometric support of the selected luxel |
| Represented stratum area | Scheduling/census coverage proxy; can include correlated or overlapping evidence |
| Physical transport patch area | Independent X09 disjoint mesh; never constructed from stratum weights |
| Statistical weight `w_i` | Dimensionless fit/evaluation mass |
| Unsupported area | Missing evidence; neither zero light nor represented certified coverage |

For selected independent representatives on block `b` and receiver surface `s`,

```text
w_i = a_i / sum(a_j for selected j on the same (b,s))
sum(w_i on (b,s)) = 1
```

Adding correlated copies cannot add votes or objective mass. This is a
surface/block-balanced weighting policy, not an unbiased whole-map area
estimator. Whole-map quality must still report geographic support. The hidden
provider receives actual area plus a separate explicit statistical weight; the
joint surface objective receives the statistical measure. Neither is reused as
reflector area. Repeated nonzero correlation IDs for one observation channel
are rejected by `Joint::Load`, rather than silently amplifying evidence.

## Stock regression

All seven named maps were re-imported from existing complete private archives.
Old and new counts below use the same source bytes, materials, receiver mapping
and immutable split. Counts are unique selected rows, not independent physical
constraints. Every selected region set is explicitly incomplete.

| Map | Old train/validation/held-out | Hierarchical train/validation/held-out | Training jobs admitted / 8 | Jobs with >=4 positive frozen validation AND held-out receivers / 8 |
|---|---:|---:|---:|---:|
| mp/duel4 | 34 / 24 / 18 | 113 / 93 / 72 | 8 | 8 |
| t1_inter | 13 / 9 / 13 | 436 / 110 / 97 | 8 | 8 |
| t2_rancor | 49 / 18 / 31 | 335 / 132 / 127 | 5 | 5 |
| hoth3 | 512 / 181 / 147 | 412 / 239 / 227 | 8 | 8 |
| hoth2 | 177 / 56 / 61 | 189 / 157 / 173 | 1 | 5 |
| vjun2 | 418 / 149 / 154 | 264 / 213 / 225 | 5 | 8 |
| mp/ffa3 | 376 / 124 / 127 | 408 / 218 / 226 | 8 | 8 |

The starvation comparison uses **the same new job bounds** for both samplers:

| Map | Old rows inside each new neighbourhood | New effective discovery rows |
|---|---|---|
| mp/duel4 | 2,10,4,5,3,6,3,7 | 23,27,11,14,11,13,7,14 |
| t1_inter | 0,0,1,0,0,1,0,0 | 48,48,48,48,48,43,48,48 |
| t2_rancor | 0,2,0,1,0,1,2,0 | 48,48,48,2,6,1,48,48 |
| hoth3 | 4,4,5,2,4,5,4,2 | 48,48,48,48,42,48,36,24 |
| hoth2 | 0,1,3,0,3,1,2,0 | 48,37,5,3,5,2,2,1 |
| vjun2 | 1,0,0,1,1,3,0,0 | 48,41,24,7,2,3,1,48 |
| mp/ffa3 | 1,4,5,3,5,1,0,2 | 48,48,48,43,48,35,23,25 |

The previously reported maxima 4/3/4 used the **old hidden-search bounds**;
they are not the denominators of this paired comparison. Whole-map training
count can decrease while useful local support increases, as in hoth3/vjun2.

Each job records same-material support, receiver surfaces, normal bins,
baseline, contrast, blocks, censoring and frozen evaluation signal in
[stock.json](../tools/rend2/observation_sampling/results/v1/stock.json).
Examples: admitted t1_inter jobs have 23–48 samples in their largest material
group and contrasts 0.108–0.624; one remains single-normal. Hoth2's first job has
48 same-material samples but zero useful contrast, so it is rejected. Five of
eight evaluation neighbourhoods have positive independent signal, but only one
has adequate training evidence. Rancor retains three unsuitable jobs; denser
sampling does not cure missing support there.

Up to four admitted jobs per map compare production `Hidden::Generate` calls
with identical geometry bounds, assumed profile, 64-node work policy and
nuisance freedom. New proposals appear where old samples were insufficient.
Counts, unavailable statuses and ray work are retained for **both** methods.
Stock source truth/XYZ accuracy and source recall are `null`, not zero or 100%.
This benchmark stops at conditional proposals; it does not claim stock joint
convergence, source discovery accuracy or calibrated existence confidence.

## Controlled compiler experiments and counterexamples

The existing pinned q3map2 BSPs are reused, not recompiled or retuned. Nine
fixture scopes at two discovery budgets (16/48 per job), up to four jobs and
64-node candidate searches yield 18 native experiments. Every candidate
prediction uses the production C++ hidden provider/forward evaluator.
Private source answers are opened only by the controller after freezing.

Spatial candidate recall uses one-to-one maximum-cardinality matching at the
declared grid-cell half-diagonal, approximately 117.60 WU, and at one quarter,
29.40 WU. These are coarse search-resolution diagnostics, not fixture-fitting
XYZ acceptance tolerances. Exact proposal duplicates are counted once; remaining
unmatched hypotheses are explicitly reported. Type/existence inference is not
being scored as solved.

| Fixture scope | New proposals, budgets 16 / 48 | Closest truth distance WU | Recall at grid / quarter radius | Positive independent evaluation? |
|---|---:|---|---|---|
| point_room | 16 / 16 | 24.41 | 1 / 1 | Yes |
| two_colour | 30 / 31 | 24.41,18.87 | 1 / 1 | Yes |
| spot | 0 / 0 | Unavailable | 0 / 0 | No; zero positive training signal |
| door_occlusion | 12 / 14 | 24.41 | 1 / 1 | No; only one positive selected held-out receiver |
| wall_leak | 16 / 16 | 41.47 | 1 / 0 | No; selected held-out receivers are dark |
| bounce_colour | 17 / 18 | 42.38 | 1 / 0 | Yes |
| bounce_colour_left | 20 / 19 | 48.61 to the remote source | 1 / 0 | No held-out geometry in the selected half-space |
| bounce_control | 16 / 16 | 42.38 | 1 / 0 | Yes |
| skylight | 0 / 0 | No local point-source truth | Not comparable | No; black positive-light benchmark is invalid |

Old sparse selection produces no proposals in these bounded scopes. New point
and two-colour coverage improves, but respectively 15 and 28/29 unmatched
alternative proposals remain. This is candidate-pool recall, not source-count
precision. Increasing discovery rows from 16 to 48 does not improve the closest
coarse-grid XYZ; candidate search resolution is then the limitation.

`bounce_colour_left` uses predeclared receiver geometry `x < -32` WU. It has no
local lamp in the trusted answers; the real source is outside that half-space.
The provider nevertheless proposes 4/5 source-like hypotheses inside it. This
is an explicit GI/transport false-source risk. There is no independent held-out
support there, and no proposal is promoted. A correct next stage must compare
source-linked transport, strict direct prediction, source-removal models and
independent observations. Neither contrast nor a converged candidate score
certifies a physical lamp.

The spotlight fold and black skylight control are preserved as limitations.
They are not positive spotlight/skylight successes. Existing fixtures cannot
establish every requested positive test under their frozen folds. A separately
declared new positive spotlight/sky benchmark would be needed; changing these
folds after seeing the result would invalidate this comparison. An analytic
material-boundary negative test verifies that different constant material
colours alone do not admit a source neighbourhood. No new bake formula is added.

Full numerical/source hashes, positivity, saturation, receiver surfaces,
unmatched alternatives and ray counts are in
[compiled.json](../tools/rend2/observation_sampling/results/v1/compiled.json).

## Limits, lifecycle and reproduction

Default API bounds: 65,536 geometry records, eight jobs, 96 coarse rows,
48 discovery/64 position/24 boundary/24 validation/24 held-out rows per job,
24 surfaces and 12 blocks per local fold, two refinement rounds, 1,024 unique
global rows, 64 MiB accounted scratch and 16 million work units. Stock adapter
uses a 32,768-record pool and 2,048 global rows. Each local signal refinement
examines at most 512 candidates. Exact footprint preparation has its own
128-million index-work ceiling. No global luxel-by-source matrix is added.
Budget exhaustion/cancellation do not turn partial output into complete coverage.

Stock child limits are 600 seconds / 1 GiB actual process RSS; compiler cases
180 seconds / 512 MiB. Only the explicitly owned child can be terminated.
Actual Windows peak working-set measurements, wall times and numerical work
are distinct from accounted STL scratch. They include archive/geometry loading,
both sampling paths and bounded provider comparisons, not just the sampler.
Wall time is measured; isolated CPU time and optimization speedup are unavailable.

The frozen repeated runs measured:

| Map | Local cell WU | Eligible / deferred neighbourhoods | Sampling work units | Whole child wall seconds, two runs | Maximum actual RSS MiB | New provider rays, attempted jobs |
|---|---:|---:|---:|---:|---:|---:|
| mp/duel4 | 362.1 | 25 / 17 | 79,216 | 2.000 / 1.421 | 21.86 | 2,684 / 4 |
| t1_inter | 55.5 | 479 / 471 | 4,189,376 | 5.234 / 4.937 | 28.95 | 5,238 / 4 |
| t2_rancor | 90.5 | 240 / 232 | 1,902,610 | 5.547 / 5.141 | 112.91 | 4,441 / 4 |
| hoth3 | 181.1 | 853 / 845 | 725,335 | 21.047 / 16.390 | 89.66 | 4,737 / 4 |
| hoth2 | 45.3 | 811 / 803 | 593,096 | 5.828 / 5.641 | 55.85 | 626 / 1 |
| vjun2 | 49.4 | 2,381 / 2,373 | 1,619,881 | 17.109 / 17.312 | 82.14 | 2,953 / 4 |
| mp/ffa3 | 181.1 | 999 / 991 | 727,889 | 12.484 / 12.375 | 54.54 | 5,016 / 4 |

The last column counts hierarchical provider rays only, not every archive or
preparation query. The paired old work is retained in JSON. Hoth3/vjun2/ffa3
reach the geometry-pool cap (50,692 / 215,358 / 36,267 replacement/drop events).
These events are not a percentage of missing unique map area. All seven have
deferred jobs, so none is labelled a complete physical-source search.

Compiler native children measured 0.094–0.828 seconds and at most 5.32 MiB peak
RSS across both final runs; their hierarchical provider cost is 0–11,264 rays
per case. These tiny controlled scenes are not a stock-map resource forecast.

Stable sample IDs remain map/surface/page/texel identities. New plan keys cover
map, frozen partition, geometry/interpretation, policy, purposes, evaluation
reserves, ranks, training signal, work state and algorithm version. Geometry,
fold or policy mutations invalidate refinement. `Sampling` starts at version 1;
`Hidden` moves 1→2 and `Joint` 3→4 to invalidate old cache/checkpoint semantics.
The `.jkobs` encoding and raw capture format are unchanged.

## Verification and unsuccessful attempts

The final frozen run passed all **34 CTest tests** in 194.15 seconds, including
region/visibility, patch/surface mapping, offline datasets, transport topology,
hidden candidates, joint fitting, producer, amplitude/shape/point/shadow and
uncertainty tests. The new standalone sampler passed **678 assertions** twice
with identical semantic JSON. Python passed four sampling/controller tests,
seven blind-view tests and four all-stock architecture/resource tests. GCC
Release standalone and both MSBuild SP/MP Release renderer targets built.

Negative tests include hidden RGB/rank IDs, role/plan/reserve mutation,
conflicting duplicate geometry, unknown transfer, dark/saturated-only signals,
material-only colour boundaries, correlated copies, missing independent folds,
single-plane uncertainty, cancellation and work limits. Hidden-provider tests
reject repeated correlated votes and non-finite accumulated statistical mass;
joint fitting rejects duplicate correlation votes. One candidate cannot count
as recalling two truth lamps. Build/hidden/observation hashes are preserved.

The [check record](../tools/rend2/observation_sampling/results/v1/checks.json)
also preserves unsuccessful attempts. Two initial tests failed before numerical
checks with model-preparation errors after the shared P04 header update. Forced
recompilation of their test objects resolved both without weakening assertions;
the complete final suite passed. One concurrent link could not overwrite the
still-running point-refinement executable; rebuilding after it finished passed.
The first compiler-controller run accidentally specified 256 KiB instead of the
intended 512 MiB child RSS limit. It was corrected; the failed attempt is not
counted among completed experiments. A renderer command with missing CMake PATH
did not launch compilation and was rerun with the pinned toolchain path.

Disabled-path source/binding comparison against `ec272791b` found zero changes
over 27 shader programs. The previous GL 3.2 compile record was reused; X10 did
not launch the game or perform a new GPU compile. The older historical frozen
shader baseline's drift remains explicitly failed in its original record; this
comparison does not overwrite it or imply a gameplay visual test.

Both seven-map stock runs and both 18-case compiler runs have identical semantic
outputs. Timing/RSS stay outside the equality witness. These are sampling and
candidate-coverage regressions, not 18 successful inverse reconstructions.

Exact build, blind-publication, stock/private and compiler commands are in the
[tool README](../tools/rend2/observation_sampling/README.md). Versioned public
JSON witnesses contain aggregate/hash evidence; private TSV retains stable IDs,
XYZ, folds, correlation groups and all three distinct area/weight fields.
No stock BSP, lightmap or triangle payload is committed.

Remaining work is conditional stock model validation, better local scale
estimation for multimodal footprint sizes, separately frozen positive spotlight/
sky folds and scheduling deferred shards. The initial pool is bounded rather
than a complete all-luxel discovery pass. Exact visibility, UnknownProfile,
material uncertainty, missing GI and source identifiability remain independent
admission/acceptance constraints. The sampler does not bypass them.
