# X06: cross-map generalization and falsification audit

2026-10-28; `rend2-unified-wip`, source commit
`fe16bce7729fd4c77b62d55abb4718da217882f6` plus the explicitly hashed, uncommitted
X05 benchmark work. This is an audit of prerequisites and conditional numerical
models. **No production inverse solver, recovered stock emitter, calibrated stock
photometry or runtime lighting integration is claimed.**

The four decisions were frozen in [baseline.json](../tools/rend2/cross_map/baseline.json)
before new capture. [policy.json](../tools/rend2/cross_map/policy.json) fixes map
priority, splits, training-only selection, budgets and admission. Actual aggregate
data and hashes: [results.json](../tools/rend2/cross_map/results/results.json);
[tables](../tools/rend2/cross_map/results/README.md);
[commands/test record](../tools/rend2/cross_map/results/validation.json).
Original X01–X05, GI, DDGI, RBDOOM, evaluation and optimization documents/manifests
are content-hashed in the baseline; their decisions or tolerances were not edited.

## 1. Frozen decisions and experimental boundary

* **A:** source-locked q3map2 profiles are verified for their tested feature
  subsets; historical stock profiles remain `UnknownProfile`.
* **B:** physical hypotheses require geometry, visibility and independent receiver
  support. RGB agreement alone does not establish source existence/localization.
* **C:** NNLS/BVLS require a genuinely linear block; geometry is bounded, profile/
  count/type comparisons are discrete, and shadow topology needs explicit treatment.
* **D:** quality comprises source accuracy, held-out prediction, coverage/
  observability, attribution and uncertainty, with no single composite success score.

Raw RGB8, source-relative decoding, calibrated scene equivalents and physically
authored SI quantities remain distinct. Grid ambient/directed are independent
observation operators, not universal indirect/direct components. Wrong-direct-
profile discrepancy is not automatically GI. Regions accelerate queries but are
not mandatory permission to fit a valid observation. Unknown optics never become
transparent to make an experiment run.

The new Python programs are orchestration/fixtures around existing capture,
`prepare_blind`, `TrainingView`, source kernels, X01 audit, X04 integration,
passive transfer and metric functions. They introduce no new observation encoding,
renderer dependency, optimizer package or second production reconstruction core.

## 2. Available maps and selection

The actual local `assets0..3.pk3` directory was enumerated, not inferred from map
names. All proposed maps exist. Frozen priority selects four SP maps:
`vjun2`, `kor1`, `yavin1b`, `t2_trip`; and three MP maps:
`mp/ffa1`, `mp/ctf1`, `mp/duel1`. `t1_sour` is available but outside the four-SP
budget. The original `hoth3`, `hoth2`, `mp/ffa3` are regression controls.

`mp/ctf1` exists in both assets0 and assets3. The assets3 override is authoritative;
its BSP SHA256 is
`375ba6a60ec25efde0a9744286d1e2998ff2a039eb9374290d0dc3b6e8d7bd36`.
The inventory reproduces exactly with archive precedence applied. PK3 hashes are
recorded in [archive-hashes.json](../tools/rend2/cross_map/results/archive-hashes.json);
every successful archive is audited against the original BSP bytes.

New licensed BSPs, raw images, grid streams, observations, blind packages and
debug OBJ/TSV stay under
`C:/Users/Keyten/AppData/Local/Temp/jk-lightrecon-x06-20261028`, outside the checkout.
Only aggregate counts, provenance, controlled own-content results and count
heatmaps are versioned. No stock PK3/BSP/lightmap is redistributed.

Actual SP map loading attempted all seven maps. MP BSPs were also read through
SP's renderer. **A native MP material/optical-equivalence check was not performed.**
MP Release compilation is a separate successful check and does not substitute
for that missing runtime evidence. MP-only entity-spawn warnings are retained;
the records distinguish world capture from gameplay correctness.

## 3. Complete observation census and a real export failure

All original colour pages were derived for six maps. The existing source audit,
offline validation, scan, facts, blind publication, public-geometry check and
area-weighted readiness evaluator were run. These are full-map censuses, not
two-page prefixes. Counts below use **original candidate colour texels** as denominator.

| Map | Colour pages | Mapped / candidate texels | Mapped fraction | Ambiguous | Unsupported | Censored samples |
|---|---:|---:|---:|---:|---:|---:|
| vjun2 | 42 | 448,811 / 688,128 | 65.22% | 40,046 | 2,310 | 62,755 |
| kor1 | 24 | 235,843 / 393,216 | 59.98% | 18,978 | 3,824 | 43,552 |
| yavin1b | 10 | 92,167 / 163,840 | 56.25% | 5,959 | 11,481 | 22,174 |
| t2_trip | 14 | 101,098 / 229,376 | 44.08% | 23,694 | 326 | 30,021 |
| mp/ffa1 | 22 | 226,573 / 360,448 | 62.86% | 1,987 | 1,295 | 9,445 |
| mp/ctf1 | unavailable | no complete archive | unavailable | unavailable | unavailable | unavailable |
| mp/duel1 | 46 | 486,958 / 753,664 | 64.61% | 4,433 | 29,647 | 78,851 |

Mapped, footprint-only, uncovered/padding, overlapping and unsupported records
are separate categories, not one missing percentage. Per-page, surface type,
style, height, normal, brightness, spatial counts and raw/source-domain flags are
retained in the private coverage reports; aggregate distributions and count
heatmaps are included with the versioned witness.

Known-footprint-area results use a **different denominator**:

| Map | Mapped area / all known footprint area | Reliable footprint area / known footprint area | Logical grid indices |
|---|---:|---:|---:|
| vjun2 | 96.35% | 94.48% | 152,500 |
| kor1 | 98.82% | 96.09% | 562,326 |
| yavin1b | 99.34% | 99.32% | 798,537 |
| t2_trip | 96.30% | 96.11% | 1,047,540 |
| mp/ffa1 | 97.74% | 93.82% | 118,320 |
| mp/duel1 | 99.80% | **67.47%** | 25,584 |

These percentages exclude unknown area and do not measure total world receiver
coverage. `mp/duel1` is a useful counterexample: high mapped count/area does not
establish a similarly reliable footprint model. Numerous approximate footprints
remain. Grid index counts are neither independent observations nor certified
free-space measurements. Their raw indexed records/directions were validated;
geometry classification and angular quantization remain separate.

All six have zero area that is unconditionally source-linear comparable under
the historical transfer/primaries metadata. All available original RGB remains
quantized; no renderer gamma/overbright is reapplied to manufacture comparability.
Deluxe directions are unavailable on these captures, not zero vectors. `kor1`
adds a real style case: 164 source style-1 channels and 10,951 separately styled
candidate texels; style-0 fitting excludes them instead of summing animation.
All maps contain patches; moving/deformed receiver counts and material flags
remain explicit. `vjun2` has 846 moving-model and 39 deformed surfaces; the
static visibility scene excludes/marks those contributions conservatively.

### mp/ctf1 falsifies the blanket export-readiness assumption

Three actual map-load attempts fail with **`Section chunk limit`**:

1. All derived pages with default 256-radius region ROI.
2. One derived page with the same region policy.
3. All pages with an explicitly tiny 16-radius region ROI.

Thus simply reducing the derived page prefix or region ROI is not a remedy.
The original BSP has 7,240 surfaces, 726 patches, 49 triangle soups, 18 raw RGB8
pages (role not independently classified after the failed export), 22,131 source
grid records and 180,668 indices. The diagnostic reports **15,106 candidates,
27,539 evidence records and 14,209,715 owned evidence bytes**. These are hypotheses,
not recovered sources. The largest source surface has 423 vertices.

`offline.cpp` uses the eight-MiB section codec limit and serializes the whole
evidence collection as one `Evidence` object; domain and region objects are also
monolithic. The large evidence collection is a concrete suspect. The generic
error does **not identify the failing section**, so this audit does not claim
to have proven its exact serialized byte count. Better section diagnostics and
chunking are required before S02 can promise all-map archival. No global limit
was raised and no incomplete `.jkobs` was accepted as success. The raw BSP-only
census for this failure is explicitly less capable than the six successful maps.

## 4. Geometry stress and independent falsification

ROI selection is fixed from training geometry/metadata: a 256-unit cell with
diverse normals/surfaces, most upward receivers, uncertain optics, and a fixture
neighbourhood, with spatial separation where available. Each uses radius 256,
coarse/minimum 128/32, patch tolerance 8 and four sky samples. Labels are **stress
candidates**, not certified room/doorway/exterior semantics. Candidate aperture
links occur in several selected graphs; they do not certify transport or spawn
portals. Outdoor/alpha/curved cases are selected without treating upward normals
as proof of sky visibility.

The existing X01 tool rechecks original geometry/domain/dynamic metadata, builds
direct and replay graphs on matched inputs, emits cells/geometry, and compares
known opaque planar contacts against an independent plane/barycentric reference.

* **24 new ROIs**, 6,139 sampled segment queries.
* **2,104 opaque reference contacts; zero false-clear witnesses**.
* Maximum matched contact-position discrepancy **1.51e-12 world units**.
* Every direct/replay classification matches; unknowns remain in the result.
* Three additional fresh regression ROIs yield 441 opaque reference contacts,
  also zero false-clear witnesses.

This does not prove no leaks anywhere: strict-interior planar reference contacts
exclude edge ambiguity and unsupported optical/curved behaviour. New ROI graphs
have 1–12 regions. Significant Solid/Unknown volumes remain; unequal adaptive
cell counts must not be interpreted as volume fractions. Exact clipped volumes,
components/apertures, local optical/moving uncertainty, occupancy disagreements,
grid/surface assignment and memory are included per ROI in JSON. Certified-free
membership is never used as proof of a direct ray.

The regression captures retain their exact original counts: hoth3 403,084/622,592;
hoth2 79,565/196,608; mp/ffa3 301,473/425,984. They were not recaptured or relabelled
as new evidence. The new regression geometry ROIs use the same frozen selection
function; they are not numerically compared with unrelated historical ROI counts.

## 5. Positive stock direct-light search: meaningful attempt, negative admission

The optimizer-facing package is the existing sanitized `TrainingView`. Selection
and nested folds use only its RGB plus geometry-only footprint metadata projected
for those training IDs. **Outer validation/held-out files are never opened by the
stock trial.** The final controller census reads their counts/brightness only
after the hypotheses/trials are frozen. No source entity truth or compiler recipe
is supplied for a stock profile experiment.

Frozen search: at most eight fixture neighbourhoods per map, radius 768, up to
four spatially separated glow fixtures and two normal offsets (1,4 world units),
48 geometry/normal-stratified receivers per nested role, 16,384-ray map cap.
Nested block/gap/seed/origin are 512/4/20261028/(91,113,157), with three-footprint
filter exclusion. Admission needs positive training signal, multiple normals,
two blocks per fold, a distance ratio >=1.25 and at least eight common-visible
receivers per role for two different source hypotheses. No threshold was relaxed.

| Map | Outcome | Neighbourhoods attempted | Rays visible / blocked / unknown |
|---|---|---:|---|
| vjun2 | No admitted region | 8 | 5 / 2,270 / 5,677 |
| kor1 | No legitimate glow priors from current provider | 0 | not queried |
| yavin1b | Fixture neighbourhoods lack nested receivers | 8 | 0 queries; insufficient support |
| t2_trip | No legitimate glow priors | 0 | not queried |
| mp/ffa1 | No admitted region | 8 | 0 / 2,595 / 6,397 |
| mp/ctf1 | Archive unavailable | 0 | not queried |
| mp/duel1 | No legitimate glow priors | 0 | not queried |

The 16,944 actually queried segments are a useful failure witness for this
specific centroid-point strategy. Both 1/4-unit offsets were tested; unknown
rays were never changed to clear. Repeated selection, masks and admission results
are identical. No stock RGB fitting/ranking is produced from an inadmissible fold.

This does **not** establish that every JA map lacks identifiable direct lighting:
geometry stress rays are frequently visible, many training receivers are positive,
and physically relevant sources need not coincide with available glow centroids.
The candidate strategy, missing priors, spatial fold support and optical knowledge
are limiting. Conversely, thousands of hints (vjun2 9,566; yavin1b 1,135; ffa1
2,335) do not establish thousands of lamps. Before S04, inspect fixture shape,
solid clearance and hint provenance; allow conservative non-glow hypotheses
supported by receiver/occlusion evidence. Do not loosen visibility or invent
point sources to force this search green.

## 6. New controlled compiler falsification

Ten independently compiled own-content BSPs reuse the pinned NetRadiant executable
SHA256 `894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4`.
Each recipe records executable/map/shader/texture/BSP hashes and exact argv.
Geometry/source answers remain controller-only, separate from newly generated
sanitized blind inputs. No modern q3map2 result is called Raven evidence.

New scenes: two coloured lights, direct versus one bounce, changed reflector,
oblique spotlight/axis, coloured gamma/clipping, strongly saturated white,
one-unit wall/doorway, positive diffuse sky roof, and one versus two co-located
white sources. Existing X05 panel, sun, sky aperture/sealed controls and X04's
35 boundary texels remain independent regression tests; native tube and general
sun/area optimization are not newly claimed.

Held-out errors below are area-weighted distances to the declared truncating
encoded-byte intervals. Fixed geometry/profile experiments fit one shared
non-negative scalar on training only; validation ranks and held-out confirms.

| Case | Correct direct RMSE | Zero RMSE | Fitted isq held-out | Fitted linear held-out |
|---|---:|---:|---:|---:|
| two coloured lights | 0 | 18.9965 | 0 | 11.4024 |
| same lights, one bounce | 4.3392 | 23.3692 | 1.7511 | 15.4108 |
| same bounce, red material | 3.2361 | 22.1740 | 1.4933 | 13.8187 |
| oblique spotlight | 0 | 11.9773 | 0 | 5.7133 |
| coloured gamma=2.2/clipping | <1e-12 | 154.4184 | 0 | 8.2263 |
| thin wall and doorway | 0 | 72.3321 | 0 | 17.0742 |
| positive diffuse sky | 0 | 25.7329 | not an attenuation test | not an attenuation test |
| white saturation | <1e-12 | 254.7277 | multiple viable | multiple viable |

Three counterexamples matter:

1. **GI biases an otherwise correct direct fit.** With fixed primary lights,
   bounce adds mean encoded deltas (5.5910,3.0759,5.1012). Fitting only direct
   amplitude raises its shared gain to **1.2058**; with a red reflector it is
   **1.1457**. A better residual does not recover the authored amplitude.
   Changing reflector texture reduces green/blue by mean 2.0490/3.9657 codes.
   These are paired encoded differences, not linear irradiance subtraction or
   exact physical bounce replay. The passive source-linked model remains energy-
   consistent on new compact geometry; compiler discrepancy must remain separate.
2. **Saturation removes discrimination.** The white scene has 72/99 held-out
   channels at 255 and the remainder near the shared-normalized endpoint.
   Both attenuation hypotheses remain viable. Gamma/output and source amplitude
   cannot be certified from an almost constant normalized white target. No forced
   singleton or posterior is reported.
3. **Source count can be unobservable.** One 300-parameter source and two
   co-located 150-parameter sources produce byte-identical original lightmaps.
   ID/order-independent ground-truth matching is still useful, but authored-count
   recall alone must not certify identifiability or penalize every equivalent
   explanation as a uniquely wrong physical layout.

An initial `sky_offset` authoring attempt referred to a missing sky shader and
compiled a dark target. It is retained as **invalid positive source input**, not
a successful sky formula or new negative physical scene. The separate
`sky_roof_v2` explicitly binds the existing source-locked sky declaration and
has nonzero signal. This caught an authoring failure, not a reason to alter X05
admission thresholds.

The thin-door all-visible approximation has held-out RMSE **7.2414** and fails
the frozen gate. The correct reference remains zero. New analytic moving-edge
positions -0.15/-0.05/+0.05 have positive integrated derivatives
0.171726/0.181358/0.190406, independently checked by central differences with
relative error <=1.11e-10. Existing cusp and fixed-sample zero-gradient controls
are rerun; these smooth-branch checks are not universal differentiability claims.

## 7. Field and evaluation assumptions

Exact support tests distinguish a sealed wall (only dark support visible), an
open doorway (both supports visible), an unknown grate and missing geometry.
With every support blocked, the exact field is `Unavailable`; normalized tiny
moment display weights can still return 100. Unknown geometry returns a status
and uncertainty, not a numerical black field. Open-door interpolation is an
allowed support interpolation, not a predicted GI solution.

Four new differently coloured angular lobes test diffuse SH separately from
incoming radiance:

| SG sharpness | SH degree | Diffuse relative RMS | Incoming-radiance relative RMS | FP32 RGB bytes/support |
|---|---:|---:|---:|---:|
| 8 | 2 | .01746 | .57514 | 108 |
| 8 | 4 | .00252 | .20025 | 300 |
| 48 | 2 | .05436 | .92575 | 108 |
| 48 | 4 | .02351 | .79922 | 300 |

L4 improves diffuse error but still loses sharp radiance. Choose field bandwidth
by quantity/error and retain explicit light or sufficiently directional records
for specular/fog/shadow consumers. Neither SH nor moment visibility localizes an
emitter. No field codec or runtime shader was added.

The existing metric framework evaluates new blind controlled inputs. Black and
one-sample predictions fail all required positive contracts. Extra false sources
and an indirect-only model can be assigned identical oracle RGB but retain source
false positives or zero recall. These are **metric plumbing controls**, not fitted
reconstructions; point-type projections here test count matching, not oblique
spot-type recovery. Existing type/geometry/uncertainty unit tests remain separate.

Bounce targets correctly fail a direct-only X05 forward contract; this is an
unsupported model component, not a request to relax the threshold or invent GI
lights. Gamma=2.2 uses its declared encoded operator and is `NotComparable` to
the frozen gamma=1 acceptance profile. Unknown stock photometry never receives
absolute candela/lux/nits error. No heuristic confidence becomes a probability.

## 8. Four-decision change control and implementation consequences

| Accepted decision | New evidence / independent coverage | Assessment | Upcoming code impact | Severity | Timing |
|---|---|---|---|---|---|
| A: tested kernel subsets, stock UnknownProfile | New multi-light, oblique spot, gamma and thin-wall bakes match; saturated scene preserves alternatives; six new stock captures still lack known transfer/recipe | **Confirm; qualify discrimination** | S01 keeps feature-level support, output domains and alternative profiles; no global Raven default | High | Before S01 profile admission |
| B: physical claims require geometry/visibility/independent evidence | 24 new ROIs certify local statements with zero sampled false-clear; 24 fixture neighbourhoods yield no admissible direct comparison; three maps have no glow priors | **Confirm; candidate sufficiency assumption contradicted** | S04 must not depend exclusively on glow centroids; add shape/solid-clearance and alternative candidate support. S02 must retain local Unknown and optional regions | High | Before stock S04 claims |
| C: separate linear, bounded nonlinear, discrete and topology blocks | Shared-amplitude controlled fits distinguish laws when informative; saturation/co-located scenes tie; bounce changes fitted direct gain; new derivative signs agree | **Confirm; qualify physical interpretation** | S01/S09 must diagnose rank/gauges and profile/GI discrepancy before physical publication; NNLS only on fixed genuinely linear operators | High | Before S01/S09 confidence claims |
| D: source/heldout/coverage/attribution/uncertainty separately | New maps have 44–65% count mapping but no unconditional comparable linear colour; full ctf1 export fails; co-located sources share identical lightmaps | **Confirm; qualify count truth and readiness** | S02 needs chunking/section diagnostics; S09 reports equivalent hypotheses; S10 fields need unavailable/quantity/components/bandwidth metadata | High | Chunking before S02 all-map promise; ambiguity before benchmark certification |

None of A–D is replaced by a visually convenient heuristic. Concrete prerequisites:

* **S01:** keep `UnknownProfile` and typed incomparable values; positive/censored/
  held-out admission and physical publication are separate. No solver success for
  observations, candidates, empty assets or conditional low RGB error.
* **S02:** fix or explicitly gate monolithic section limits; test assets3 overrides,
  style-1 receivers, approximate footprints and native MP optics. Preserve full
  source data even when regions/derived components fail, with explicit partial status.
* **S04:** do not assume a complete fixture prior list or centroid ray suitability.
  Candidate count is not emitter precision; missing priors are normal. New candidate
  methods need the same geometry/positive/held-out tests, not unbounded budgets.
* **S09:** direct gain shifts under known bounce; constrain passive transport and
  preserve signed model discrepancy/reflectance/profile alternatives. Do not turn
  an equivalence class or censored plateau into calibrated confidence.
* **S10:** low-order diffuse compression, radiance bandwidth and exact support
  validity are separate. No supports is unavailable; component membership prevents
  direct/sky/GI double counting. Consumers remain independently opt-in.

## 9. Resource and reproducibility record

Frozen budgets: seven new maps; 24 GiB new data; 4 GiB process peak; 600 s external
stage; 1,800 s total engine capture allowance; four stress ROIs/map; eight direct
search neighbourhoods/map; <=16,384 source queries/map. No 620-configuration
optimizer sweep was run on stock maps.

* Nine actual engine invocations (seven maps plus two ctf1 retries): **193.89 s**
  summed wall time. Maximum engine peak working set **1,532,899,328 bytes**.
* Successful capture archives: **1,580,846,976 bytes** total. Additional scan/blind/
  analysis files: **3,435,412,340 bytes**, excluding hardlinked PK3s and later small
  debug/query outputs. Hardlink apparent size is not counted as new physical data.
* Full census controller: **1,022.86 s**, peak **268,992,512 bytes**. Per-map
  77.88–309.23 s includes validation, scan, partition/readiness and charts.
  Individual external-stage times/RSS are in JSON; peak RSS excludes child
  processes, whose own measured peaks are separately recorded.
* New geometry audit children: **526.80 s** summed for 24 ROIs (two matched builds
  plus diagnostics each); largest child peak **80,646,144 bytes**.
* Repeated stock candidate search: **50.78 s**, peak **394,719,232 bytes**;
  selections, ray states and admission are identical after excluding timing fields.
* Ten new compiler controls plus bounded profile experiments: **48.30 s**, Python
  peak **28,037,120 bytes**. Compiler child RSS was not measured in that aggregate.
* New controlled blind/metric checks: **9.33 s**, Python peak **30,556,160 bytes**.

Full BSP recompilation determinism is not inferred: the known compiler can vary
grid compression/near-solid records and unused brush-side references. Every new
BSP is fingerprinted individually; known-source paired comparisons explicitly
check receiving geometry/UV identity. Repeated analysis uses frozen bytes.

## 10. Tests, failures and remaining unknowns

New X06 tests cover baseline/content freeze, archive precedence, geometry-only
selection, deterministic splits, field support, passive transport, derivatives,
new controlled positive/ambiguous cases, blind controls and independent stock rays.
Existing evaluation, blind, bake/profile, X04, GI, DDGI, SH and optimization unit
tests were run, together with 17 CPU CTest targets and both SP/MP Release targets.
Disabled-path preprocessing checks cover 27 programs / 51 stages with zero diffs;
GPU compilation/reflection was deliberately not run. Exact commands/results and
test counts are in the validation record.

The first optimization-suite attempt could not import the owner-only existing
SciPy install in the sandbox. The same 30 tests then passed in the package owner's
context. No ACL change, package installation or network access was performed.

Unresolved: ctf1 archival failure, native MP material equivalence, semantic room/
doorway labels for every stress ROI, accurate alpha/transmission/curved optical
models, informative stock profile ranking, complete fixture priors, stock source
ground truth, absolute calibration, general bounce replay and full-map fitting
cost. No gameplay image validation or physical emitter reconstruction was done.
The research decisions survive with stricter prerequisites; **general stock-map
solver readiness has not been demonstrated**.

Reproduction commands and source/answer separation:
[cross-map tool guide](../tools/rend2/cross_map/README.md).
