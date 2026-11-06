# X16: blind medium-BSP discovery and structural selection

The new opt-in proposal policy finds all three local source neighbourhoods within
32 world units on both frozen V02 bridge variants. It does **not** recover an
accepted complete lighting configuration. The spotlight's direct contribution is
zero on every frozen validation receiver, and the bounded catalog and finite
fixture model still have limitations. Original-stock physical accuracy remains
unknown. No game or reconstructed-light consumer was launched for this task.

## Protocol and evidence boundary

Parent revision: `785bf36eda94be32dc534f26a71e22c481b0758f`, with the X16 source
changes recorded by content hashes in the result manifest. CPU: GCC 16.2 Release,
C++11; renderers: MSVC 2019 x64 Release / Windows SDK 10.0.17763. Host: Windows
10.0.19045, Intel Family 6 Model 165 Stepping 2, 12 logical CPUs. Other work ran on
the host; process times are observations, not an isolated throughput comparison.

The original 1536 x 1024 multichamber compiled BSPs, private answer, eight local
search requests per variant, and spatial folds are unchanged:

| Variant | Original BSP SHA-256 |
|---|---|
| direct | `57f0827f4a62f9b1ce0de1d0e29ab66cbe0bfcff26b85d461a6114bec5185279` |
| passive_bounce1 | `3087ba242708a781deb0bbe439091112a067b2befd5341856afe11dd13ce48a3` |

There are 5,470 mapped original observations and frozen 64/34/56 selected
training/validation/final-held-out rows. Discovery replays original public
geometry and training requests. The final controller obtains fold geometry from
the public **discovered-proposal** request; it never uses the oracle-XYZ request.
Private source answers are read only by post-fit reporting and the explicitly
labelled supplied-answer observability control. In the final controller, validation
targets open on validation callbacks; held-out targets open after the preferred
model parameters freeze. Earlier trusted-controller runs loaded evaluation rows
earlier but did not supply their values to numerical fitting or admission.

Policies v1-v6 and unsuccessful experiments are retained. v1 used four common
refinement tickets and regressed the bounce variant's first point proposal to
42.418 WU. v2 kept eight tickets and initially exhausted the mandatory 12M-ray
joint budget. v3 reduced the **coarse** amplitude work for every compared model;
strict evaluation and acceptance thresholds were unchanged. v4 added exact-recipe
legacy and eight-ticket controls. v5 fixes diagnostic provenance, scopes finite
families by geometric connectivity, exports alternative models and strengthens
held-out file admission. These are sequential development experiments, not
independent statistical trials; there is no held-out-based tuning of coordinates,
folds, thresholds or per-map exceptions.

v6 adds post-freeze reference diagnostics for at most three alternatives in
their pre-existing validation order. Source parameters and final predictions are
exported after Solve. Final scores do not reselect the preferred model or change
any fitted parameter. Post-freeze work is counted separately.

## Why the spotlight proposal was missed

The old generator aimed both spot trials at one total-RGB brightness-weighted
receiver centroid. Its two trials differed only in radius (64/128 WU, target
distance 128 WU). Sparse floor/black-wall support did not provide a useful axis
initialization. Point and spot positions also competed for four refinement
tickets, and the old downstream bridge admitted four separated **points only**.

The opt-in policy uses one old centroid axis and one negative, brightness-weighted
receiver-normal axis. The latter is a seed derived from training geometry, not
independent proof of emission direction. Both use the same declared 128/64
historical target/radius. The number of trials per position remains one point
plus two spots. Kind-interleaved, unique-pose refinement uses up to eight tickets;
maximum nodes, rays, calls and memory are unchanged. An eight-ticket legacy-axis
control separates work allowance from the geometric-axis change.

Two production conversion defects were also fixed:

* Hidden spot target distance/radius now survive S05 annotations and
  `Producer::MakeCandidate`. A full cone display angle cannot replace the
  compiler's target/radius and additive 16/32-WU angular transition.
* Hidden `SurfaceObservation.surfaces` identify illuminated **receivers**. They no
  longer become mounting fixture surfaces or `VisibleFixture` evidence. Actual
  authored/visible fixture support still uses existing placement and wall checks.

Versions change to Hidden 3, Candidates 2 and Producer 8. Relevant settings,
historical angular parameters and policy identities participate in dependency
hashes. Existing cached conversion results cannot silently acquire new semantics.

## Proposal measurements

Distances below are the minimum geometric distance across the public proposal
pool, in WU. They are not final XYZ error, source existence probability or type
recovery. The finite emitting prism is reported separately; it is not included in
the three-local-root recall denominator.

| Variant / policy | Candidates | Point A | Point B | Spot | Recall <=32 WU | Rays |
|---|---:|---:|---:|---:|---:|---:|
| direct / frozen | 208 | 6.270 | 11.803 | 74.343 | 2/3 | 146,640 |
| direct / legacy axes, 8 tickets | 215 | 6.270 | 11.803 | 74.343 | 2/3 | 203,136 |
| direct / geometry axes, balanced 8 | 194 | 6.270 | 11.803 | 11.800 | 3/3 | 215,376 |
| bounce / frozen | 218 | 22.788 | 21.432 | 75.676 | 2/3 | 162,456 |
| bounce / legacy axes, 8 tickets | 217 | 22.788 | 21.432 | 71.318 | 2/3 | 221,448 |
| bounce / geometry axes, balanced 8 | 217 | 22.788 | 21.432 | 11.800 | 3/3 | 240,000 |

The nearest new **typed spot** is also 11.800 WU from the spot answer, with a
downward axis; the original two point neighbourhoods remain in the full pool.
This is a proposal result. Cone, colour, source count and existence still require
independent fitting. The final JSON retains typed distances, axes, underconstraint,
all profile-conditioned counts and far-from-answer hypothesis counts. A far
proposal is not a physically published false emitter.

All new bridge hypotheses remain `AmbiguousSourceOrTransport` and position
underconstrained. None acquires an existence probability. Pure `TransportEvidence`
is deferred rather than submitted as a physical emitter by the controller.

All comparisons use the same per-job caps: 384 nodes/area, 64 receivers,
100,000 forward calls, 2M rays, 64M traversal work, 64 MiB accounted scratch.
These are equal **caps**, not equal actual work: new axes use more rays than the
frozen run. The frozen executable reproduces the old geometry; the new adapter's
legacy mode reproduces it while exporting previously omitted angular metadata.
All 64 original/legacy/equal-cap/new jobs repeat with identical candidate IDs,
parameters, support, model keys, forward counts, rays and traversal work.

## Catalog and supported source kinds

The controller ranks training shape scores within each shard/kind, then assigns
round-robin tickets, cap 16 and at most two per shard/kind. Unknown scores sort
last. Reordering records does not change admission. S05 performs real canonical
consolidation, provenance mapping and selection before `Producer::MakeCandidate`.
Spatial proximity alone does not merge fixtures. Same sampled point/spot poses
are numerical type alternatives, not a recovered source identity.

This is a bounded screening policy, not final source certification. It still
loses useful seeds. In the new inverse-square direct catalog, nearest typed
distances are 6.270/23.018/12.701 WU; in the bounce catalog they are
42.418/59.155/11.800 WU. Thus full-pool 3/3 recall must not be reported as 3/3
active-catalog recall. Deferred records remain available with IDs, profile,
shard, support and reason; they are not rejected physical sources. Progressive
training-residual catalog expansion remains necessary for reliable weak-source
retention in this bridge.

New full pools contain 110 point/84 spot (direct) and 117 point/100 spot (bounce)
proposals. Each profile's 16-ticket catalog retains 8 of each kind. Respectively
176/194 and 208/217 full-pool hypotheses are farther than 32 WU from all three
local answers. These are exploratory alternatives, including other potential
explanations; calling their complement physical-emitter precision would be wrong.

Point/spot alternatives retain exact historical recipes. Optional finite trials
use original planar emissive-face triangle support and existing SourceSupport
mounting/side rules. The rectangle enum is a numerical tag; emission is integrated
over the leased original triangles, not a bounding rectangle. Same-material faces
sharing a full geometric edge form conditional assembly groups with stable local
IDs. Different disconnected fixtures do not share one map-global family. Faces
within a group are competing partial-shape models; the complete nonplanar prism,
its compiler facet subdivision/GI and physical root count remain unsupported.
No six-point surrogate or invented bounding panel is used. A finite trial cannot
establish accurate cylindrical emission where original geometry does not support it.

## Joint selection, positive evidence and the validation limitation

`JKJOINT16` invokes production Joint with point/spot/finite alternatives,
activation/removal, merge/split proposals and optional bounded geometry refinement.
Inverse-square and linear profiles run separately with identical nuisance policy.
There is no free per-luxel GI, gamma or residual illumination. Both direct and
bounce observations are tested against an explicitly limited direct-only model;
the bounce variant does not become a validated passive-GI reconstruction.

The bounded revised schedule is 3 rounds, beam 2, 128 models, at most 8 active
sources, 2 amplitude sweeps x 12 line steps and 1 geometry sweep when requested.
Limits are 200k evaluations, 12M rays, 1B work, 128 MiB and a separate 600 s
process safety stop. Sources cannot ignore mandatory receivers. Final interval
MSE must be <=1, <=2% of black MSE, with positive evidence and required spatial
blocks passing; missing predictions are not black.

The supplied-answer strict forward **observability** check establishes:

| Direct root | Positive train /64 | Positive validation /34 | Positive final /56 |
|---|---:|---:|---:|
| Point A | 14 | 4 | 8 |
| Point B | 6 | 4 | 10 |
| Spotlight | 3 | **0** | 2 |

Every validation prediction for the true narrow spotlight is exactly zero under
the scoped direct operator; all 34 predictions are available. Consequently this
validation set cannot distinguish that source's correct direct light from its
absence. Complexity can prefer two points even with a nearby correctly directed
spot proposal. The answer was used only to diagnose observability after blind
search. The frozen fold was not moved and final RGB was not used to rescue it.
This is a limitation of this frozen direct-light benchmark protocol, not proof
that the actual source is absent or that all indirect effects are identical.

The revised inverse-square direct runs prefer two conditional points, with 18
retained alternative configurations. Both old-proposal/kind-aware and new-proposal
fixed-geometry catalogs give final interval MSE 31.7844 against black 232.25,
worst-block RMSE 28.9454. That is improvement over black but **fails** the
declared final gates. Model counts are 90/91, strict evaluations 51,953/53,776,
rays 7,373,830/8,853,232. No physically exact sources are published. The same
catalog comparison under linear falloff has no preferred admissible model.

The revised bounce/direct-only run retains a partial three-source configuration,
final MSE 32.2156 vs black 380.625, worst-block RMSE 28.9454. Against the known
local-root answers it has TP=0, FP=3, FN=3 at the declared geometric/type gate,
and is **not accepted**. The direct two-point configurations have TP=2, FP=0,
FN=1; compiler-Q RGB parameter errors are about 8.41%/7.66%. These are conditional
preferred-model diagnostics before final acceptance, not published source recall.

Final-adapter geometry/finite work exhausts the 12M native-ray cap after 27
attempted models. Its six original-face alternatives retain explicit
`Clearance`/`UnknownGeometry` exclusions: adjacent visual geometry is not a
certified supported fixture volume. The complete prism cannot be recovered by
declaring those checks clear. A fixed-geometry final-adapter run hit the separate
600 s wall safety bound; its incomplete native work and final score remain
unavailable. This must not be replaced with the interim v3 score. Per-run policy
and executable hashes distinguish these experiments in the results.

An independent final-adapter repeat completes in 328.994 s with 91 models,
53,776 evaluations and 8,853,232 rays. It reproduces the two-point parameters and
31.7844 held-out MSE exactly, with 17,649,664-byte peak RSS. It retains a
two-point-plus-spot alternative with training objective 2.0553 rather than
35.2460, but exactly the same validation objective 0.318626. This exposes the
source-count ambiguity under the frozen validation protocol. The earlier wall
stop remains recorded; a faster repeat does not erase it.

The final v6 run completes in 359.013 s and preserves the same preferred model,
work counters and final score. Three alternatives are then evaluated **without
reselection**. The two-point-plus-spot alternative has position errors
6.270/23.018/12.701 WU, spot axis error 0 degrees, full outer-cone error 2.083
degrees and spot compiler-Q RGB relative L2 error 11.86%. Its final interval MSE
is 3.50999 vs black 232.25, worst block 6.7934; required blocks still fail and
MSE exceeds 1. This is genuine blind structural fitting of a spot alternative,
not supplied geometry, but it is not a complete accepted reconstruction. The
preferred two-point model remains unchanged. Post-freeze reference work is
308/462/462 rays for ranks 0/1/2, outside the reported fitting counters.

The complete case table, including timeouts, early mandatory-budget failures,
bounce cases, final-controller reruns, type/count attempts and rejection reasons,
is in [the machine-readable results](../tools/rend2/discovery_x16/results/v1/results.json)
and [generated tables](../tools/rend2/discovery_x16/results/v1/tables.md).
Missing final scores remain unavailable. Solver convergence is not existence
calibration; XYZ interval coverage and calibrated probabilities are unavailable.
The encoded residual cannot certify original source identity or absolute lux.

## Regression and scope

All 45 standalone CPU tests pass (446.63 s in this concurrent run). SP and MP
Rend2 Release DLL builds pass. New regressions cover opt-in axis/kind work,
ordering/key determinism, exact historical angular parameter propagation,
receiver-versus-mount provenance and invalid angular metadata. Existing tests
cover opaque walls, legitimate doorway transport, colour/material uncertainty,
bounce-only evidence, zero signal, invalid positions, cancellation and budgets.
The Python controller tests cover shard/kind diversity and fold-value isolation.

Eleven combined Python tests pass, including producer-controller and V02 report
regressions. The final native diagnostic exporter is exercised on the same blind
case, independently of the unchanged renderer builds.

Supplied-XYZ point_room, two_colour and door_occlusion compiled-fixture producer
controls are rerun separately with wrong-profile negatives and deterministic
canonical archive repetition. These controls test amplitude/forward/publication,
not blind XYZ discovery. Their results and resources are exported separately.

The supplied-XYZ compiler-Q RGB relative errors are 0.0484% for point_room and
door_occlusion and 5.16%/5.86% for two_colour. Final encoded interval MSE is
0/0.222869/0 respectively. All three wrong-profile controls are DiagnosticOnly;
the repeated two_colour canonical archive is byte-identical. These successes do
not supply XYZ or source count to the blind search.

## Cost and incomplete work

The repeated new proposal jobs take 22.361 s total (direct) and 20.204 s (bounce),
versus 16.563/16.438 s frozen. Peak proposal process RSS is 7,110,656/7,061,504
bytes; accounted per-job scratch upper bound is 11,409,920 bytes. New work is
2,024/2,216 positions and 215,376/240,000 rays across eight jobs. RSS and accounted
memory are different quantities and are not summed across sequential jobs.

Revised completed joint cases take 377-507 s in the concurrent measurements,
with 12.85-17.68 MB peak observed RSS; timer stops near 600 s are separate from
deterministic native work-limit stops. Strict evaluation/cache counters are in
the case records. The direct new interim case uses 155,398 preparations and
250,829 geometry-cache hits. These small selected-row problems already require
millions of rays; no claim of stock-map throughput follows. There is no global
all-source/all-luxel dense matrix. Unknown work after a killed process stays null.

No renderer equations, normal illumination, shader baselines, gameplay movers,
sun direction or lighting consumers change. No game launch or runtime screenshot
was performed; this task is CPU/numerical validation.

## Reproduction and retained private receipts

Use the bundled Python and w64devkit/CMake on PATH. B is the existing private
`jk-lightrecon-v02-bridge-v2` directory, and OLD contains the frozen baseline
Hidden executable. Every output must be a fresh directory:

~~~text
cmake --build build/s06-gcc --parallel 2
ctest --test-dir build/s06-gcc --output-on-failure -j2
python -X utf8 -m unittest tools.rend2.discovery_x16.test_contract
python -X utf8 tools/rend2/discovery_x16/run.py --bridge B --baseline OLD/lightrecon_hidden_tool.exe --tool build/s06-gcc/lightrecon_hidden_tool.exe --output PROPOSALS
python -X utf8 tools/rend2/discovery_x16/verify.py --previous PREVIOUS_PROPOSALS --repeat PROPOSALS --output repeat.json
python -X utf8 tools/rend2/discovery_x16/fit.py --bridge B --proposals PROPOSALS --tool build/s06-gcc/lightrecon_joint_tool.exe --output FIT_ISQ --profile 0
python -X utf8 tools/rend2/discovery_x16/fit.py --bridge B --proposals PROPOSALS --tool build/s06-gcc/lightrecon_joint_tool.exe --output FIT_LINEAR --profile 1
python -X utf8 tools/rend2/discovery_x16/oracle_scope.py --bridge B --tool build/s06-gcc/lightrecon_bake_forward_tool.exe --output ORACLE_SCOPE
python -X utf8 tools/rend2/discovery_x16/report.py --bridge B --proposals PROPOSALS --fits FIT_ISQ FIT_LINEAR --oracle ORACLE_SCOPE --output REPORT
python -X utf8 tools/rend2/producer/benchmark.py --tool build/s06-gcc/lightrecon_joint_tool.exe --out SUPPLIED_CONTROLS
python -X utf8 tools/rend2/discovery_x16/collect.py --bridge B --proposals PROPOSALS --fits FIT_ISQ FIT_LINEAR --oracle ORACLE_SCOPE --controls SUPPLIED_CONTROLS --repeat repeat.json --output REPORT
~~~

Actual private receipts use TEMP prefixes `jk-lightrecon-x16-`: proposals-v1/v2/v4/v5,
fit-isq-v1, fit-isq-v3, fit-linear-v3, fit-final-v5, fit-final-repeat-v5,
frozen-diagnostics-v6, oracle-scope-v1,
supplied-controls-v1, repeat-v1.json. Public results contain metrics and hashes;
no original JA stock geometry is committed. Initial invalid finite centre-bounds
and unknown-score sorting failures remain in the early receipts.

The isolated renderer build uses `build/x16-renderers`, Visual Studio 16 2019 x64,
SP/MP Rend2 only, engines/game modules off. With MSBuild node reuse enabled the
initial builds failed creating tracker files with AccessDenied. Retrying with
`MSBUILDDISABLENODEREUSE=1`, `/nr:false`, `/MP2` and sequential targets completed;
no other agent's processes were terminated. Commands/logs are recorded in the
validation manifest. These are build-environment failures, not source passes.

Remaining scientific work is independent positive spotlight validation on a
future, separately frozen dataset; better progressive catalog admission under
the same error policy; bounded geometry/finite-source search that completes;
validated multipart historical emission and material-linked passive transport;
then uncertainty assessment and full-model publication. No X16 result demonstrates
original Raven source recovery on stock maps.
