# Reusable reconstructed-lighting validation V02

2026-11-04. Parent revision **f031927a3**: V03 was committed before this work.
The [frozen policy](../tools/rend2/validation/policy-v1.json) predates these
measurements. [Measured results](../tools/rend2/validation/results/v1/results.json),
[generated tables](../tools/rend2/validation/results/v1/tables.md),
[stock CSV](../tools/rend2/validation/results/v1/stock.csv) and
[source/artifact hashes](../tools/rend2/validation/results/v1/manifest.json)
accompany this report. Stock exports contain aggregate diagnostics, not original
map geometry, lightmaps or private answers.

## Conclusion

**Original stock physical lighting is not restored.** Eight original maps enter
Joint; none publishes an accepted nonempty model or reaches final held-out
evaluation. Original stock XYZ/type/count and absolute SI accuracy remain
Unavailable. A new blind compiled bridge finds nearby proposals for two of
three local lights, misses the spotlight and fails discovery-based joint
selection. The full source-discovery acceptance gate therefore fails.

Controlled fixed-XYZ fitting, conservative component composition, prepared
visibility, sufficiently dense controlled fields and persistent loading work
within the scopes below. Sparse fields and general stock exact-light promotion
are not deployment-ready. Loading an asset or producing smooth RGB does not
change this conclusion.

This change only adds test/tools and updates a shader-test fixture. It does not
alter production renderer illumination, shadows, sun, fog or lightmaps. The
inverse solver still ends at the canonical asset; GPU capacities are not
optimization constraints.

## Environment, manifest and gates

Intel Core i7-10750H, nominal 2.60 GHz, 12 logical processors,
25,592,647,680 physical bytes (23.83 GiB), Windows 10 build 19045,
Python 3.12.14. CPU native tests: GCC 16.2 Release/C++11. SP/MP Rend2:
MSVC 2019 Release, /MP2. Driver shader tests: Intel UHD Graphics,
27.20.100.8280. Other agents share the machine; scheduling, CPU frequency and
GPU load are not isolated. Reported latencies are measured distributions,
not hard realtime guarantees.

The frozen 57-map v4 inventory is unchanged: 34 SP / 23 MP,
29,147,136 original colour texels, 17,718,741 mapped luxels. V02 selected hoth3,
hoth2, vjun2, kor1, yavin1b, t1_danger, mp/duel4 and mp/ffa3 before scoring.
The other 49 are **not rerun**, not failed or successfully dark. Original
extraction is reused, so new fitting times exclude capture/extraction.
The stock MP captures retain their original SP material interpretation; actual
MP runtime checks do not retroactively verify those optics.

The runner consolidates CTest, S11/S12/S13 controllers, P01/P02/P03/P05/P06
benchmarks, the X15 stock adapter, V01 cache and existing image/shader checks.
There is no Python reconstruction optimizer. Native hashes, policy, baseline
hash and core inputs are frozen in protocol.json; final source hashes identify
the uncommitted V02 code in addition to its parent commit.

For each declared receiver/profile compare observations with
Encode_profile(local direct + direct sun + sky + conditional passive bounce +
explicit supported nuisance). Signed encoded discrepancy is an error, not
positive physical GI. RGB8 intervals/shared-peak encoding remain the production
operator's responsibility. Encoded source RGB cannot generally be added or
subtracted as physical energy.

For physical fields, E(x,n) = integral L(x,w) max(0,n dot w) dw. Directions point
from receiver toward incoming light; emission axes follow photon travel.
Irradiance SH needs cosine convolution. Physical Snapshot APIs retain Lux/Nits
and compatible linear Rec.709 requirements. Conditional Q/appearance fields use
the separate typed opt-in API with explicit configurations.

Predeclared V02 gates: diffuse relative RMSE <=10%, radiance <=15% where
comparable, no missing mandatory predictions, zero known-wall false clear,
area visibility absolute error <=0.035, tube <=0.04, zero query allocations and
soft p95 <=250 us. Native numerical assertions are unchanged. Blind local
proposal recall must reach 80% within 32 WU. Positive held-out controls are
mandatory; an all-black negative scene cannot demonstrate source fitting.
Process completion and scientific quality have separate statuses.

Supported configuration policy:

| Configuration | Evaluation scope |
|---|---|
| Legacy lightgrid | Historical evidence only, not physical GI or calibrated lux |
| Explicit emitters | Controlled physical/historical models; supplied XYZ clearly labelled |
| SH L1/L3/L8 | Angular comparisons; no production spatial codec claim |
| SH L2/L4 | Production diffuse fields with independent angular/spatial admission |
| Fitted SG | Angular comparison; general production spatial SG backend unsupported |
| Explicit plus residual SH | Known explicit roots, separate low-frequency membership |
| Geometry/visibility disabled | Deliberately unsafe leakage controls, not deployable |
| GI off/on | Supported passive source-linked transport, no free patch emission |
| Cache off/on | Same supported source/receiver identities; prepared queries versus geometry |

## Stock pipeline coverage

| Stage | Frozen 57-map baseline | V02 selected cohort |
|---|---:|---:|
| Complete original capture | 57 | same 57 inputs reused |
| Geometry/readiness | 57 | 8 newly evaluated |
| Local search attempted | varied historical stop points | 8 |
| Scene coordination attempted | 0 | 8 |
| Joint::Solve entered | 0 | 8 |
| Direct-source forward inside Joint | 0 | 6 |
| Independent validation callback | 0 | 8 |
| Accepted-model uncertainty assessment | 0 | 0 |
| Frozen final held-out evaluation | 0 | 0 |
| Accepted nonempty canonical asset | 0 | 0 |
| Prepared stock physical lighting field | 0 | 0 |

The historical stops (49 sharding, three local support, three transport overlap,
one solar, one folds) remain unchanged. Validation callbacks here include
empty/rejected hypotheses and do not establish a physical model.

| Map | Jobs/profile | Jobs proposing candidates isq/linear | Joint calls, both | Supported mapped area | Seconds | Native RSS MiB |
|---|---:|---:|---:|---:|---:|---:|
| hoth3 | 697 | 146 / 146 | 8 | 52.99% | 34.375 | 109.30 |
| hoth2 | 573 | 15 / 15 | 8 | 56.05% | 13.281 | 81.05 |
| vjun2 | 1896 | 306 / 306 | 8 | 61.59% | 36.922 | 113.83 |
| kor1 | 374 | 38 / 38 | 8 | 59.39% | 19.938 | 93.83 |
| yavin1b | 156 | 17 / 17 | 8 | 51.17% | 7.438 | 46.42 |
| t1_danger | 81 | 20 / 20 | 8 | 3.76% | 41.687 | 57.44 |
| mp/duel4 | 20 | 4 / 4 | 8 | 62.74% | 5.531 | 34.77 |
| mp/ffa3 | 968 | 370 / 370 | 8 | 72.12% | 29.704 | 79.41 |

Area is supported observation-chart area / mapped observation-chart area,
not disjoint reflector area, searched volume or fitted-light coverage.
All searches are bounded and incomplete. Across two independently scoped
profiles: 9530 local invocations, 23788 local proposal forward calls, 77333
reported rays, 64 Joint calls, 66 fitting forward calls and 2296 fitting rays.
Placement attempts include 46 conditional, four proven and 74 collision
checks; these are repeated proposals, not real fixture counts.

| Map | Train / validation / final geometry rows | Positive rows in those partitions |
|---|---|---|
| hoth3 | 412 / 239 / 227 | 266 / 177 / 186 |
| hoth2 | 189 / 157 / 173 | 121 / 112 / 139 |
| vjun2 | 264 / 213 / 225 | 229 / 192 / 184 |
| kor1 | 402 / 228 / 225 | 360 / 199 / 193 |
| yavin1b | 278 / 143 / 148 | 278 / 143 / 148 |
| t1_danger | 244 / 105 / 66 | 244 / 105 / 66 |
| mp/duel4 | 113 / 93 / 72 | 98 / 81 / 70 |
| mp/ffa3 | 408 / 218 / 226 | 361 / 197 / 209 |

These controller partitions precede the mandatory common-coverage gate.
Final RGB never guides discovery, fitting or sampling admission; positive counts
are evaluation diagnostics. The machine-readable report preserves map/BSP
hashes, exclusions, required blocks, model attempts, reasons and resource counts.

Current blockers:

- hoth3: most tested placements fail clearance; a conditional linear model uses
  59/64 required rows and is rejected.
- hoth2: examples use 51/64, 52/64 or 46/64 rows after conditional placement.
- vjun2: every tested activation fails clearance.
- kor1: 103 coarse rows are required; tested models omit 9–13.
- yavin1b: tested models omit 7–20 of 64 rows.
- t1_danger: activations are inside known solids; the declared historical
  operator supports only 3.76% of mapped area.
- mp/duel4: 55/64 and 60/64 prediction witnesses remain explicit rejections.
- mp/ffa3: unknown geometry/clearance and conditional 58/64 support.

All eight transport preparations produce locally supported links (159–15907),
with unsupported area and omitted links retained. Scene fitting is still a
conditional direct-only hypothesis. A valid transfer graph does not verify
stock material reflectance, compiler bounce or GI amplitudes. Sun is UnknownSun
and environment unresolved. Held-out errors, source-count truth, physical
confidence distribution and exact-light suitability remain unavailable.

## Blind independently compiled discovery bridge

The owned 1536 by 1024 WU multi-room BSP has two openings, a sealed compartment,
several receiver orientations, two differently coloured point lights, a downward
spotlight and a shader-emitting finite prism. Variants use zero/one compiler
bounce. Binary/source locks and map/BSP/shader/answer hashes are retained.

Private source answers freeze before compilation/search. Native Hidden receives
only geometry-only .jkobs and training RGB, never light entities, answer XYZ,
amplitude/count or hidden RGB. The material attestation names only actual owned
white/red/panel shaders and emissive geometry, not source photometry. Wrong-map,
unknown-material and duplicate-surface declarations are rejected. The BSP tool
now copies original UV/style data; it does not invent normal interpolation.

Each variant maps 5470 receivers; frozen selected folds are 64/34/56. Eight
geometry shards use <=64 training rows and 384 search nodes each. Candidates
freeze before private matching. The narrow downstream adapter admits at most
four separated discovered point candidates in stable order; this catalog limit
is separate from full proposal coverage and is a remaining limitation.

| Variant | Jobs with proposals | Hypotheses | Nearest proposals to three local roots, WU | Recall <=32 WU | Discovery fit |
|---|---:|---:|---|---:|---|
| Direct | 7/8 | 208 | 6.270 / 11.803 / 74.343 | 2/3, fails 80% | Unavailable |
| Compiler bounce 1 | 8/8 | 218 | 22.788 / 21.432 / 75.676 | 2/3, fails | Unavailable |

188/209 proposals are farther than 32 WU from each local point/spot root.
These include alternatives and possible surface explanations, not proven
false physical lights. Final selected-source precision, count/type, intensity
and interval coverage are unavailable. The prism is a separate authored
emitting object; its complete physical/source count recall is unsupported by
the point-only adapter, not silently counted correct.

Three capabilities are evaluated separately:

1. Blind discovery: proposal recall above; final selected model unavailable.
2. Supplied XYZ point-only refit: intentionally wrong spotlight type and absent
   prism. The direct variant retains a partial two-point model, held-out MSE
   29.939 versus black 232.25, worst block RMSE 28.945. It fails required quality.
   The bounce variant is unavailable.
3. Supplied-answer native point/spot forward control: correct local XYZ/type/Q,
   but prism and compiler bounce omitted. Held-out MSE is 0.392857 versus
   black 232.25 (direct), 27.5655 versus 380.625 (bounce). Worst block errors
   2.708/13.026 fail complete coverage-quality acceptance. This is a partial
   local-root oracle, not full-scene truth or discovery.

The benchmark exposes proposal/catalog/type-selection gaps without feeding
source answers back to discovery. It does not demonstrate complete blind source
recovery or absolute SI calibration.

## Controlled source, sun, shadow and uncertainty results

The compiled S13 fixtures execute actual Producer/Joint, final independent
evaluation, contribution datasets, uncertainty and serialization:

| Fixed-XYZ fixture | Held-out interval MSE | Black MSE | Relative fitted RGB error | Correct-profile output |
|---|---:|---:|---|---|
| point_room | 0 | 2495.889 | 0.04836% | Conditional asset |
| two_colour | 0.222869 | 1170.602 | 5.162% / 5.864% | Conditional asset |
| door_occlusion | 0 | 2495.889 | 0.04836% | Conditional asset |

Wrong supported profiles fail independent selection. Repeated two_colour
archives are byte-identical. XYZ is a supplied prior, not recovered ground truth.

S11 controls cover same/different colours, point/spot/area, adjacent panels,
tubes, bright plus weak, missing-source addition, removal/count ambiguity,
unknown scale, saturation, local plus sun, grid/deluxe operators, walls/openings
and passive transport. Held-out MSE / black MSE examples:
1.2444e-6 / 0.7560 (two colours), 2.7888e-5 / 0.1206 (point/spot/area),
1.8771e-10 / 2.0896 (bright plus weak). Constrained position refinement has
zero sampled XYZ error. These are compact supplied-prior controls. Coincident
count stays Unidentifiable; zero signal InsufficientSignal; ambient-only has
no invented physical source.

Solar passes 17631 native assertions. Multi-normal sun angular error:
0.22238 degrees, relative sun RGB error 1.3649%, with 12 alternatives. Sun through
an opening with an indoor lamp: 0.18170 degrees. Positive sky-only stays SkyOnly,
relative sky RGB error 0.2712%. Saturated/single-normal/reflected/contradictory
coloured-sun cases abstain. This does not identify Raven's compiler or validate
unprofiled grid directions as source truth.

The 289-check automatic-shadow suite compares intensity only, centre visibility,
footprint visibility, oracle correspondences and automatic correspondences.
At seed X=-52, the first three retain 20 WU error; oracle/automatic reach zero
within a fixed-height one-dimensional test, using 892 versus 66 rays. The
reported two-colour association subset has TP=2, FP=0, FN=0, TN=2. An undisclosed
albedo boundary remains a possible edge, never a verified shadow. Three
source-motion derivative steps differ <=4.58e-5 from reference derivative 2.
Finite-source width matches 8 WU with explicitly approximate quadrature.
This is not unconstrained stock shadow localization.

S12 repeats disjoint procedural calibration/evaluation families. Raw candidate
precision is 11/46 (23.91%), truth coverage 11/17 (64.71%), 35 false and six missed
candidates. Retained complete configurations cover 9/14, not nominal 95%.
Empirical-rate threshold 0.9 accepts three with one false; conservative
lower-bound threshold 0.5 abstains on all. This supplied-position population
does not justify calibrated stock confidence or exact-light promotion.

## Angular and spatial fields

P03 repeats 18 reference scenes across SH L1/L2/L3/L4/L8, fitted SG1/3/6,
SG3+SH2 and explicit+SH2. Training: 3200 directions; independent angular tests:
8192 directions, 129 normals and a 32768-direction reference. SG axes are fitted.
Pointwise delta radiance is NotComparable.

One three-coloured-lobe case:

| Representation | Diffuse relative RMSE | Radiance relative RMSE | FP32 encoded bytes |
|---|---:|---:|---:|
| SH L1 | 28.453% | 92.936% | 80 |
| SH L2 | 5.423% | 86.154% | 140 |
| SH L3 | 5.423% | 77.038% | 224 |
| SH L4 | 1.750% | 66.803% | 332 |
| SH L8 | 0.219% | 27.741% | 1004 |
| Fitted SG3 | 0.0496% | 0.2354% | 144 |
| Fitted SG6 | 0.1579% | 1.3640% | 228 |
| SG3 + SH2 | 0.0108% | 0.2318% | 252 |

L3 adds no diffuse benefit here because the higher odd cosine terms vanish.
More SG lobes do not necessarily improve a bounded fit. Per-scene CSV/JSON keeps
ringing, false-dark illumination, energy bias, colour/dominant/peak-direction
errors, fit work and FP32/FP16/scaled-integer quantization. Sizes include
headers, not GPU atlas allocations. BC6H/device compression is unperformed.

P01 tests independent positions/normals:

| Spatial field case | Relative error and availability | V02 quality |
|---|---|---|
| Point / spot | 1.520% / 1.520% | Pass where supported |
| Rectangle / tube | 1.323% / 2.034% | Pass |
| Partially occluded area | 4.225% | Pass diagnostic |
| Constant sky E / L | absolute 8.74e-8 / 0 | Pass |
| Coarse passive bounce | 39.628%, 30/30 points | Fails 10% |
| Fine passive bounce, 512 probes | 6.343%, 30/30 | Pass |
| Sparse spatial stress | 26.742%, 720/1800 supported | Fails error and coverage |

1080 absent samples remain unavailable. L2 direct projection rejects eight
narrow/high-frequency probes instead of manufacturing energy. Point build:
7592 accounted bytes, 16 rays, 4856 integration work, maximum angular RMSE
0.04472, energy bias 0.001705. Higher SH order cannot fix absent spatial support.

## Geometry, probes and visibility

P02 passes 8274 assertions. Constant error zero, linear-gradient RMSE 0.110826,
directional-colour spatial RMSE 0.00278511 in the fixture's units. Point-field
RMSE decreases 2.35675 to 0.934711 under refinement, not a universal relative
guarantee. 9409 wall-link checks have zero leaks; doorway links survive.
Adaptive probes increase 64 to 1408; 256 relocate by <=0.25 WU. Original baked
observations are not moved.

Three geometry-only quantile-selected ROIs per stock map:

| Map | Probes | Supported / analyzed WU cubed | Strict / conditional supported queries of 3072 |
|---|---:|---|---|
| hoth3 | 1830 | 34972808 / 402653184 | 0 / 262 |
| vjun2 | 2481 | 75737448 / 402653184 | 0 / 573 |
| mp/duel4 | 2225 | 187867952 / 402653184 | 1445 / 0 |

ROIs may overlap: sums are not unique map volume. These datasets contain no
stock radiance coefficients. Conditional geometry, unknown opacity and movers
remain distinct; opening support does not establish correct light intensity.

P05 passes 262 assertions. Area visibility 0.408740 versus independent 0.399225
(absolute error 0.009514); tube 0.558671 versus 0.553533 (0.005138).
64 opaque tests have zero false-clear; doorway positive/negative controls pass.
Two optical cases remain Unknown. 1184 source-probe rows split 592 visible/
592 blocked. Area/tube builds use 1280/1216 rays. Tube coarse/refined difference
0.039299 is uncertainty, not a statistical bound.

Float64/UNorm16 encodings use 980/908 bytes in the compact case. Moving a source
rebuilds one row and reuses two; RGB changes reuse three. At 1/8/32 sources,
32 receivers each, work is 32/256/1024 rays and 13312/106496/425984 accounted
bytes. This is sparse-entry scaling, not arbitrary all-luxel scalability.
PVS, no-hit rays and moments do not become universal transmission certificates.

## Indirect attribution and contribution safety

339 passive-transport assertions pass. Finite-reflector integration differs
0.2821% from independent reference; reference orders 48/64 differ 1.06e-7.
Red-reflector held-out MSE is 1.33e-16 versus direct-only 3.9846e-5 and black
0.233308, reflectance approximately [0.7000003,0.1800000,0.0800009].
A sealed wall transfers zero, an opening 0.00557813 red; unknown geometry keeps
0..0.00557813 bounds. Hidden-source doorway bounce has zero receiver direct,
0.00197298 indirect, no aperture emitter. Wrong-profile error is not verified GI.

P06 passes 497 assertions including 14 unsafe-replacement adversaries. An
authored point-plane sample contributes 6.25 lux and is eligible only on that
comparable support. Actual two-colour Producer attribution prepares 60 samples/
120 records, 20584 accounted bytes, 336 rays/four forward calls; maximum
root-reference discrepancy zero. Sparse absence is unknown, explicit blocked
zero remains zero.

Shared-peak encoding exposes unsafe subtraction: pre-encoded red 510 gives
green 127.5 after encoding, while independently encoded source greens sum to
255. Algebraic equality is not physical conservation. Double counting, unknown
membership, exclusive configurations, discrepancy-as-GI and conditional stock
replacement are rejected. No stock source is exact-replacement eligible.

## CPU latency, cache and lifecycle

4096 batches of eight queries run on actual prepared backends after warmup.
Known fixture source positions are supplied for performance, not discovery.

| Query | Successful / attempted | p50 / p95 / p99 us | Allocations |
|---|---:|---|---:|
| Snapshot | 32768 / 32768 | 0.125 / 0.125 / 0.125 | 0 |
| Incoming radiance | 32768 / 32768 | 2.375 / 2.625 / 3.250 | 0 |
| Conditional field | 32768 / 32768 | 2.250 / 2.375 / 3.000 | 0 |
| Static visibility | 32768 / 32768 | 0.125 / 0.125 / 0.125 | 0 |
| Source contribution | 32768 / 32768 | 0.250 / 0.375 / 0.500 | 65536, fails |
| Probe lookup | 32768 / 32768 | 0.250 / 0.375 / 0.375 | 0 |
| Indirect on sky-only field | 0 / 32768 | unavailable rejection | 0 |
| Missing region provider | 0 / 32768 | unavailable rejection | 0 |

P06 copies domain strings, making two heap allocations/query. Fast timing does
not pass the allocation contract. Indirect-only physical and installed-region
latency distributions are unperformed here. Query code does not call disk,
fitting, global traces or GL; no OS-wide I/O interception experiment was run.

V01 persistent correctness: 154 checks pass, including semantic dependencies,
SP/MP context, corruption/limits, atomic replacement, sharing denial/concurrency
and interrupted writes. Exit-23 after flush and three-check preservation pass.
Cold field+shadow build: 0.224701 s, 1024+1 rays, 741312-byte archive.
Warm: zero solver steps/rays, 11022 allocations, 4843238 allocated bytes,
5953704 reservation, 16072704 peak process bytes. Cold allocations: 170783.

| Resident load | Mean open ms | Mean prepare ms | Bytes read | Payload chunks |
|---|---:|---:|---:|---:|
| Metadata | 0.1971 | 0.1792 | 5909 | 0 |
| Field | 0.2517 | 14.8361 | 740332 | 17 |
| Visibility | 0.2151 | 0.3740 | 6851 | 2 |
| Both | 0.2543 | 15.4123 | 741274 | 19 |

Mean release: 1.9/134.7/4.2/145.8 us. Separate 100000-query conditional mean
5.42 us is not substituted for distributions. RSS in wrapper phase receipts
measures that process, not an entire descendant process tree; native child
receipts provide the relevant field/stock/cache memory.

Actual hoth3 renderer tests load an explicitly empty I/O diagnostic asset,
not fitted stock lighting. Disabled performs no dependency/cache/build work;
cache-only miss is Unavailable, zero solver steps. Warm hit hashes 30723030
dependency bytes (~0.2553 s), reads 53337 cache bytes, opens in 12.472 ms,
prepares in 2.695 ms. First snapshot: 1.2127 s; launch/script: 8.836 s;
peak process RSS 419.9 MiB. Exposure retains the key and zero solver work.

Actual warm-cache vid_restart, same-map reload and disconnect pass: Available/
Ok/zero steps before/after restart and reload, Unloaded after disconnect.
CPU tests separately cover stale snapshots/leases and selective payloads.
This is production metadata cache validation, not source recovery or a fitted
stock field. No process-wide heap leak detector was run.

## Runtime and shader regression

Actual SP hoth3/hoth2, legacy and modern PBR/Forward+/fog/sun, ran at 640x360,
picmip 3, 30 FPS, shadow size 256, all effective reconstruction consumers off.
All four existing A/B/A image/permutation/FX positive-control cases pass the
unchanged one-code/five-pixel/1e-5-mean tolerance. Images were inspected:
hoth3 interior/character/fixtures and hoth2 exterior snow/fog are visible.
The outdoor camera does not validate every sun-shadow edge.

MP local-server runs on mp/duel4 and mp/ffa3, legacy/modern, completed and showed
disabled consumers, but **visual regression remains failed/unvalidated**.
Initial screenshots were covered by player setup; later world-visible captures
had cl_paused=sv_paused=0 and failed the freeze gate. A/A drift cannot be
attributed to reconstructed lighting. All invalid attempts remain recorded;
none is a passing screenshot comparison.

Repeated private executable paths produced unwanted Windows launch/network
permission prompts. Further MP launches stopped. The runner now excludes all
game phases by default; MP requires an explicit phase and --allow-mp-launch,
uses one executable path per batch, and records remaining cases Unperformed
after an invalid control. No firewall rules are changed. This revised safety
control was CPU/CLI-tested, not used for another game launch.

At build policy zero, geometry-debug preparation correctly returns Unavailable.
MP marker alignment is not claimed. V03's actual SP marker work remains a
separate diagnostic result, not V02 stock source/field accuracy.

The shader fixture was updated to recognize V03's generation-checked debug
early return and extract the production RC_* comparison header. No renderer
shader was changed. All 27 programs / 51 stages compile/link on the driver.
Disabled shader inputs/bindings versus f031927a3 have zero differences.
The older 380ee0f7 baseline still fails, 7278 diff lines; it was not rewritten.

## Repeats, failures and incomplete work

Identical inputs reproduce hoth3/mp/duel4 scheduler and Joint receipts exactly,
and all 16 Hidden searches/four Joint event streams after excluding the two
wall-clock fields. IDs, alternatives, work counters and predictions are checked.
Existing P01/P03/P05/P06/S11/S12/S13 repeat/archive gates pass.

Independent bridge rebakes are not byte-identical: lightgrid lumps 15/17 differ,
while geometry and surface lightmaps match. This retained compiler
reproducibility failure legitimately changes complete BSP hashes and stable IDs;
it is separate from passing native same-input replay. Lightgrid ordered replay/
calibration remains unsupported.

Initial tooling failures remain visible in phase_execution: zero sky-sample
argument rejected bridge geometry; selective-cache measurement initially used
the wrong subdirectory. Fresh corrected runs retain the same physics/frozen
thresholds. Raw timing comparison was corrected to exclude explicit elapsed
fields. No held-out tuning, clearance relaxation or Unknown-to-Visible change
was made.

Unsupported/unperformed: all-57 refits, 20-map visual suite, authoritative stock
XYZ/SI answers, complete medium-scene prism/bounce oracle, free blind spot/finite
shape recovery, stock physical fields, production spatial L1/SG, GPU codecs/
bandwidth, dynamic occluder updates, all requested source-evidence ablations,
and an accepted positive stock final model. No universal O(N^2)-free scaling
claim is made from the bounded sparse tests.

## Recommendations and consumer acceptance

| Future consumer | Measured guidance | Current readiness |
|---|---|---|
| Ghoul2 diffuse | L2; L4 after angular and spatial admission | Controlled CPU fields; stock photometry unavailable |
| Fog/particles radiance | Explicit sharp roots plus admitted radiance basis | Irradiance SH insufficient; general spatial SG unavailable |
| Environment diffuse | Separate sky/indirect L2 or quality-admitted L4 | Need geometry support/calibration; no RGB8-to-lux assumption |
| Environment specular | Explicit geometry/high-frequency radiance | SH2/SH4 radiance errors reject general exact use |
| PBR/LTC physical lights | Canonical geometry and calibrated suitable source | Authored controls only; no stock exact source accepted |
| Static shadows | Sparse P05 with optical/sampling uncertainty | Bounded controlled CPU support; dynamics separate |
| World diffuse replacement | P06 comparable domain/root/path/support | Controlled authored cases only; no stock eligibility |

Default offline work should retain unknowns, keep profiles separate and use
source-linked one-bounce transport only with supported materials/geometry.
Start diffuse fields at L2 with spatial refinement; opt into L4/SG after
independent error admission. Preserve sharp explicit lights. Measured sparse
cache sizes are evidence for future adapter planning, not GPU capacity limits.

Priority gaps: source-kind/mount-aware proposals, broader fair catalog admission,
complete visibility without dropout, blind spotlight/finite-shape recovery,
independent existence calibration, spatial density, allocation-free P06 queries,
compiler grid reproducibility and stable MP visual controls. No GPU consumer is
implemented or recommended as enabled by default.

## Reproduction

Use a fresh output directory, pinned compiler and original private capture root.
Full commands and per-process work are in private protocol/resource receipts.
With w64devkit on PATH, /MP2 and the bundled Python interpreter:

~~~text
cmake --build build/s06-gcc --parallel 2
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
python -X utf8 tools/rend2/validation/run.py --output <fresh-private> --private <all-stock-captures>
python -X utf8 -m unittest tools.rend2.validation.test_report
~~~

Default phases do not launch games. Explicit --phases runtime_sp runtime_cache
requires --installation, --binaries and --renderer; runtime_mp additionally
requires --allow-mp-launch, --mp-binaries and --mp-renderer. A Windows network
prompt may occur; no consent is submitted automatically. Exact --resume verifies
the same native/core identity. --stock-maps selects only a frozen-cohort subset.

Actual V02 supplementary commands:

~~~text
lightrecon_persistent_test.exe <main>/cache/owned-cache/prepared benchmark
python tools/rend2/validation/lifecycle_runtime.py --cache-run <runtime-cache-v1>
python tools/rend2/validation/run.py --output <stock-repeat-v1> --private <captures> --phases stock --stock-maps hoth3 mp/duel4
python tools/rend2/validation/verify.py --input <main> --bridge <bridge-v2> --bridge-repeat <bridge-repeat-v1> --stock-repeat <stock-repeat-v1> --exact-replay <exact-replay-v1> --output <repeat-check-v2> --native build/s06-gcc
python tools/rend2/validation/report.py --input <main> --output tools/rend2/validation/results/v1 --bridge <bridge-v2> --runtime <runtime-v1> --mp <runtime-mp-v4> --runtime-cache <runtime-cache-v1> --gpu <gpu-v3> --delta <disabled-delta-v2> --cache-log build/v02-cache-selective-v3.log --repeat <repeat-check-v2>
~~~

Private roots are under TEMP with prefix jk-lightrecon-v02-: v1 (main),
bridge-v2, bridge-repeat-v1, exact-replay-v1, stock-repeat-v1, repeat-check-v2,
runtime-v1, runtime-mp-v1/v2/v3/v4 (failed controls preserved), runtime-cache-v1,
gpu-v1/v2/v3 (fixture failures then actual compilation), disabled-delta-v2.
Local build/v02-*.log files preserve execution/build logs.

CPU: 45/45 tests, 269.82 s. SP/MP Release builds pass. Additional native
solar/shadow/transport/uncertainty controls pass. Scientific failures, invalid
MP controls and unavailable stock predictions remain explicit rather than being
converted into a global success score.

report.py --enforce writes all results and exits nonzero on any failed scientific
gate. run.py exits nonzero on failed/budget-exhausted phases. Without --enforce,
report generation is a data-export operation, not physical-quality acceptance.
The five additional Python checks include the no-implicit-game-launch contract.
