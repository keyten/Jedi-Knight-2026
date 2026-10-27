# X03: bake-profile identifiability and UnknownProfile selection

This is an offline numerical experiment and a future evaluator contract. It adds
no production inverse solver, runtime lights, renderer switches or rendering
changes. The historical source investigation remains in
[the bake-model specification](rend2-bake-model.md); its pinned catalogue and
source lock are reused without changing their historical conclusions.

Three claims have different evidential requirements:

| Claim | Evidence in this experiment | What it cannot establish |
|---|---|---|
| A: a forward rule reproduces a known compiler with known inputs | Original RGB8 from controlled, hash-checked compiler fixtures | A stock map's unknown bake recipe |
| B: one conditional model predicts withheld observations better | Same source hypotheses, nuisance budgets and spatial splits across alternatives | Historical identity or physical source truth |
| C: historical compiler/authored sources were recovered | Requires independent production provenance or externally known sources | Neither low RGB error nor a B ranking verifies C |

`UnknownProfile` remains the stock-map identity in every result. The pinned modern
compiler is a controlled reference, never the default for original JA maps.

## Implemented boundaries and profile catalogue

`tools/rend2/bake_model/hypotheses.py` resolves the existing five families into
13 named hypotheses. These are meaningful compiler options and explicitly
unattributed assumptions, not 13 claimed historical compilers:

| Family | Numeric candidates | Evidence |
|---|---|---|
| NetRadiant d9338309 JA | inverse-square/cosine, linear/no-angle, linear/cosine, inverse-square/no-angle, half-Lambert, scaled cosine, spot | `VerifiedSubset` per feature, with fixture-specific scopes |
| Original q3map | direct inverse-square/cosine, no modern FAST cutoff | Inspected source; output completion is explicitly assumed; not a JA RBSP executable verification |
| GtkRadiant q3map2 2.5.16 JA | direct inverse-square/cosine | Inspected source, not executed here |
| Unattributed assumption | inverse-square/cosine and linear/no-angle | Assumed analytical alternatives; no Raven attribution |
| Raven production unknown | No numeric rule | Historical family hypothesis, not recovered production formulas |
| UnknownProfile | No numeric rule | Missing required rules return `UnspecifiedModel`, RGB absent |

Each immutable hypothesis contains a stable ID/family, formula version,
catalogue/source-lock SHA256, 12 feature rules, feature-specific provenance,
verification scopes, unsupported cases and finite nuisance ranges. Rule states
are `Unknown`, `Assumed`, `SourceSupported`, `VerifiedSubset`; there is no global
`verified` flag. Rule selectors cover attenuation, angle, spot, point scale,
surface integration, sun/sky, grid reduction, deluxe, bounce/fill, output,
sampling/visibility and absolute calibration. Native tracing/filtering and
absolute calibration remain unknown even in the controlled modern profile.
Historical candidates do not inherit modern JA deluxe verification.

Catalogue bounds are explicit research priors, not measured parameter intervals:
RGB amplitude [0,10000] compiler-relative, geometry offsets [-128,128] world
units, gamma [1,2.2], compensate [1,2], compiler exposure [0,255], fill [0,32].
Actual trial bounds may be narrower and are recorded in each comparison policy.
The exposure argument is the compiler's `ColorToBytes` parameter, not EV/stops.
Geometry uses the existing right-handed +Z-up world coordinates. Spot axis points
along propagation; the incoming receiver-to-source direction has the opposite
sign. No experimental scalar is renamed candela, lux or nits.

## Confounding mathematics

For a point source, let Q be a non-negative RGB amplitude vector, I=max(Q),
c=Q/I, r=|s-x|, d=max(16,sqrt(r²+extra²)), a=max(0,n·(s-x)/r), visibility V.
One-sided receiver eligibility still applies when the optional angle factor is
disabled. Within the supported, unfiltered surface-direct subset:

```
inverse-square: E = V c I 7500 A / d²
linear:         E = V c max(0, A I 7500 / 8000 - d fade)
```

A is the selected angular rule. Linear intensity changes the cutoff distance:
it is **not** a free gain times a fixed `(intercept-distance)` basis. The shared
I=max(Q) colour gauge makes RGB fitting comparable between the families.
Predictions are summed before the complete output encoding, including shared
peak normalization. Modern compiler fixtures validate only the documented
subset, not arbitrary baked transport.

The exact gauges and declared countermodels in `confounding.py` are:

* At r0, K/r² has tangent `3K/r0² - (2K/r0³)r`. Over a narrow distance interval,
  a linear law with gain and intercept can approximate inverse-square to second
  order. A wider interval reveals curvature. The gain needed for this tangent
  is an explicit additional nuisance, not silently granted to just one profile.
* A floor receiver has `E(rho)=K h/(h²+rho²)^(3/2)`. Near rho=0, changing h and
  scaling I proportional to h² preserves brightness; a wider floor or several
  receiver orientations constrains the difference.
* If, and only if, an observation includes diffuse reflectance rho,
  `(rho,I) -> (rho/t,tI)` is an exact ambiguity. Ordinary JA lightmaps are not
  automatically treated as containing current PBR albedo; no division by textures
  occurs in this experiment.
* Before clipping, `255 (E/255)^(1/gamma)/compensate` is unchanged by
  `E -> E t^gamma`, `compensate -> t compensate`. Gamma itself alters spatial
  shape, but on restricted brightness intervals it also trades with intensity.
* Different sun colour normalization conventions can be absorbed into an unknown
  authored amplitude; a direct fit alone does not identify their implementation.
* A Gaussian edge with sigma² = source_extent² + filter_width² cannot separate
  filtering from source extent. This is an analytic counterexample, not a claim
  that q3map uses that Gaussian filter.
* Different grid ambient leaks plus independent ambient gain can agree exactly
  in a simple one-source reduction. Ambient/directed grid channels are not
  physical indirect/direct decompositions. Ordered grid compression and bounce
  recurrence cannot be inferred from this single-pass example.
* Co-located inverse-square sources add to one source away from individual FAST
  cutoffs. Source count, colour decomposition and coincident positions are then
  unidentifiable even without noise.

Shadow masks add constraints only if source/occluder/receiver geometry and optical
policy are credible. Binary visibility is discontinuous. These experiments use
enumerated positions/axes and deterministic bounded coordinate searches; they do
not claim Adam/L-BFGS gradients through unknown shadow edges. Bias, filter width,
geometry error and source extent must remain separate nuisance/discrepancy terms.

## Comparison domain, objective and isolation

Targets here are original encoded compiler RGB8. The assumed profile predicts
compiler-relative linear channels, then applies its output recipe before any
comparison. It is invalid to compare those raw bytes directly with physical
linear irradiance. Source transfer/primaries remain unknown on stock maps; a
conditional encoded-channel fit does not make their values calibrated or
comparable scene-linear Rec.709.

For a truncating byte b<255, the allowed output interval is [b,b+1]. At b=255 the
upper endpoint is unbounded: the sample is censored. The residual is distance to
that interval. Its zero width endpoint convention has no numerical effect on
these fixtures. For sRGB round-to-nearest use half-byte intervals instead; this
benchmark runs the documented truncating recipes. Shared peak normalization
happens before interval comparison: a saturated channel's neighbours are not
assumed to retain unclipped radiance ratios.

```
for profile in matched_profile_candidates:
    for source_configuration, output_recipe in fixed_public_search_budget:
        theta = bounded_fit(TRAIN_ONLY, profile, configuration, recipe)
        Jtrain = mean(interval_residual(encoded_prediction(theta), raw_rgb)^2)
        Jtrain += lambda_amplitude * sum((Q/300)^2)
        penalty = lambda_complexity * (k*log(1+Btrain) + log(Nconfigs*Nrecipes))/Btrain
        keep lowest (Jtrain + penalty) for this profile
    score_on_validation = validation_interval_MSE + penalty
freeze profile ranking                         # validation only
report held_out_prediction_for_frozen_models    # never used by fitting/selection
```

All profiles get identical counts, positions, axis candidates, RGB bounds,
encoding recipes, fill freedom and optimization passes in a trial. k counts
three RGB coordinates/source, three optional fill channels, varying geometry
coordinates and varying encoding dimensions. Complexity weight defaults to .25
in encoded-byte² units; a 0/.25/4 sweep exposes its effect. This transparent
regularizer is not BIC evidence or a Bayesian posterior. Fit searches are bounded
coordinate descent (coarse log grid and local ternary refinement), at most 6000
objective calls per fit and eight passes; a local minimum is possible. Numerical
search failure is not evidence against a compiler.

Synthetic folds are assigned to receiver patches before RGB generation. Three
correlated samples on one patch stay in one fold and bootstrap unit; distinct
patches have a footprint/filter gap. Compiled floor fixtures use 128-WU spatial
blocks, 24-WU boundary exclusion, deterministic hash seed 20261032 and one sample
per 64-WU cell. Actual floor footprint is 16 WU. The underlying sample positions
are reconstructed from the BSP, using the known one-unit compiler sample push.
The finite planar room cannot establish general full-map/material performance.

Stock trials use **only the X02 outer-training view**. X02 validation and held-out
target files are never opened. Nested X03 folds use 256-WU world blocks, origin
(17,31,47), seed20261027, and the existing footprint-aware exclusion rule
`3*footprint_radius + 4 WU` at block boundaries. One sample per64-WU cell is
selected by stable ID, without inspecting brightness, capped at48/fold.
The fitting function receives only the training `Prepared` object. Tests mutate
validation/held-out RGB and assert fitted parameters are unchanged; held-out
mutation also leaves the validation ranking unchanged. The controlling harness
necessarily holds evaluation answers; these are not a malicious-code sandbox.

Metrics include interval RMSE, byte RMSE, censored channels, per-spatial-block
error, fitted nuisance values, configurations and parameter counts. Paired
spatial-block resampling (128 repetitions, seed20261027) describes sampling
stability when at least three blocks exist. Its percentiles are not calibrated
confidence in an unknown stock bake or posterior model probabilities.

## Experiments and quantitative witness

The versioned scalar witness, complete metric CSV and preference table are in
[`tools/rend2/bake_model/x03/`](../tools/rend2/bake_model/x03/).
They contain no licensed lightmap bytes or BSP geometry. Full private run products
are under ignored `build/x03/accepted/` (repeat: `accepted-repeat/`).

The 14 deterministic analytical/quantized scenes cover narrow/broad distances,
both falloff families, multiple normals, full clipping, gamma, compiler exposure,
local ambient fill, spatially varying unmodelled bounce, door/wall occlusion,
unknown source count, half-Lambert and spot cones. Three protocols progressively
fix geometry/intensity, fit RGB with fixed geometry, then allow a small fixed
position/axis/encoding set. Geometry-free here means bounded nuisance search,
not an arbitrary source-layout reconstruction. Analytical scenes complement,
rather than substitute for, the real compiled RGB tests.

The eight compiled RGB comparisons are `inverse_square`, `linear`,
`linear_angle`, `no_angle`, `half_lambert`, `spot`, `colour_gamma`, `colour_clip`.
All use the frozen original BSP hashes in `validation.json`. All 22 cases are
replayed with their existing metric/tolerance policy. A separate fresh invocation
of the pinned executable bakes all22 anew (66 BSP/VIS/LIGHT invocations); its report
is `build/x03/compiler-all22/result.json`. The known 35-texel sun/wall boundary
band remains explicitly outside exact trace replay. Paired bounce tests check
added signal/grid inclusion, not a full GI solution.

Detailed held-out, confusion and sensitivity results follow in the acceptance
section below. An exact point-kernel match cannot distinguish historical
revisions whose direct-light formulas coincide on this subset.

## Stock experiment assumptions and limitations

The hoth3 ROI is centre(3320,700,123), radius1024 WU. The hoth2 outdoor-map ROI is
centre(1728,-576,1040), radius1536 WU. These deliberately include bright/dim and
diverse surfaces, not only a hand-picked bright fixture. Only planar receivers
with available matching geometric/shading normals are compared numerically.
Two nearest distinct static glow hints provide separate, fixed point hypotheses;
each is offset4 WU along its captured outward normal. Their geometry is a prior,
not an actual point source or recovered ground truth. Gamma/fill hypotheses do
not change fixture positions. Each fixture is scored independently; results
from unlike source configurations are not pooled into a global profile verdict.

The existing immutable CPU BVH/BSP queries classify every receiver-to-hint ray.
Only the common set with known visibility for both candidate configurations is
used; unknown is never clear. Opaque hits remain blocked. The globally incomplete
scene stays incomplete even for locally certified rays. No PVS test substitutes
for optical visibility. Source/receiver surface bias policy is recorded in
`lightrecon_profile_rays.cpp`; the batch utility rejects cross-map fingerprints
and sanitized datasets containing lightmap/grid targets.

There is no reference source position/intensity/production recipe on stock maps.
Unknown other lights, sun/sky, bounce, material response and bake-time differences
can dominate these simplified trials. Even a preferred family would only be B,
conditional on the fixture/configuration, source count, transfer and fill policy.
The five historical inventory maps retain `UnknownProfile`. hoth2 is also a
negative coverage result: the selected outdoor ROI has no RGB in the available
X02 outer-training partition. Outer evaluation targets were not opened to fill
that gap. On hoth3 every common certified ray to these two fixtures is blocked;
the point hypotheses have no direct response. That case returns
`NoDirectResponseOnCertifiedSubset` with an empty ranking, rather than using the
lexical order of identical errors to select a compiler. Positive target lighting
requires some other source/transport explanation. No substitute numerical error
or source truth is fabricated for absent observations.

## Recommended UnknownProfile strategy

1. Preserve `UnknownProfile`, raw bytes, unknown primaries/transfer and independent
   surface/grid calibration. Do not populate a nominal physical intensity.
2. Evaluate historically grounded **feature hypotheses** with explicit required
   rules. Unknown required features yield no prediction; unsupported data do not
   become black lighting. Keep the unattributed analytical assumptions separate
   from claims about Raven.
3. Freeze geometry/candidate-count/visibility assumptions for each trial. Fit
   shared nuisance policies on training only; evaluate on genuinely separated
   validation surfaces, distances, normals and shadow configurations.
4. Retain feature equivalence classes at several declared regret allowances
   (.25/1/2/4 encoded bytes), plus per-block errors and matched complexity scores.
   `feature_assessment()` reports attenuation/angle/spot/output sets at each
   allowance. These are operational sensitivity sets, not confidence intervals.
5. Review source priors, sampling, transfer, clipping, fill and geometry
   sensitivity before narrowing any feature. Rank reversal, insufficient coverage,
   saturation or large discrepancy retains competing hypotheses. A singleton in
   one trial alone is not historical identification.
6. For downstream inverse fitting, preserve alternatives with their fitted
   nuisance values, spatial diagnostics and profile digests. Do not let a more
   flexible source count/GI residual buy an apparent compiler identity.
7. Only independent compiler provenance can promote claim C. Known physical
   authored emission requires the existing calibration/photometry contract;
   compiler-relative amplitudes remain relative regardless of RGB accuracy.

## Future CPU evaluator and cache contract

`shared/rd-rend2/lightrecon/bake_profile.h` specifies, without implementing a
production evaluator, `ProfileHypothesis`, `FeatureRule`, `SourceHypothesis`,
`Receiver`, `EvaluationContext`, `Prediction`, `Result`, `Options` and the abstract
`ForwardEvaluator::Evaluate`. `Require` is a non-numeric preflight. It checks
schema/formula compatibility and missing feature evidence, and never reports
available lighting by itself. Unknown calibration blocks physical-unit requests.

It reuses canonical `Units`, `Calibration`, `PhotometricUnit`, `Estimate`,
`Decode::Profile`, immutable owning `Chunk<T>` leases, chunk sources, structured
`VisibilityProvider`, `ReadContext` budgets/cancellation and `Disk::Digest`.
Geometry/material/visibility providers are explicitly injected; they contain no
target RGB. Ordered grid reduction and deterministic sampling seed are explicit.
`AmplitudeDomain` distinguishes compiler-relative Q from canonical `Photometry`
(Y=1 chromaticity and explicit intensity/calibration); no cast or automatic
conversion connects them. Point/spot/polygon/tube/directional kinds do not imply
implementation support.
Output domains distinguish prequantized encoded RGB, compiler-relative linear,
source-linear relative, ambient/directed grid and deluxe direction. An unavailable
grid/deluxe result has no direction; typed ambient/directed channels, incoming
world direction and optional encoded lat/long are separate payloads. Their
quantization/error estimates default to unknown. An unavailable
prediction's zero payload is not black light. Numerical error/model discrepancy
default to unknown, not arbitrary confidence products. A future implementation
must report unsupported selectors/domains/ordered-state requirements explicitly.

The Python research evaluator implements only selected direct point/spot surface
rules with supplied certified visibility and optional bounded fill. It cannot
replay arbitrary polygons, sky sampling, bounce, ordered grid compression, HDR
compiler output, general deluxe accumulation or native shadow filtering.

Cache keys must include map/input content, geometry/material policy, observation
decode policy, feature-level hypothesis/formula version, source and nuisance
configuration, sampling seed/order, output domain and evaluator implementation
version. X03 records catalogue/source-lock/hypothesis/evaluation SHA256 and source
file hashes, and refuses to publish if Python sources change during the run.
No `.jkobs` or canonical lighting-asset disk format is changed, and no old cache
is silently reinterpreted. A future evaluator must add a versioned keyed result,
not turn conditional preference into the dataset's historical compiler identity.

## Reproduction

Python3.8+ standard library only; no optimizer, numeric or chart packages added.
From the repository root, with the existing CMake/compiler in PATH:

```powershell
python -X utf8 tools/rend2/bake_model/test_kernels.py -v
python -X utf8 tools/rend2/bake_model/test_identifiability.py -v
cmake -S tools/lightrecon_test -B build/x03-cpu -DCMAKE_BUILD_TYPE=Release
cmake --build build/x03-cpu --parallel 2
ctest --test-dir build/x03-cpu --output-on-failure -j 2

$q3 = Resolve-Path build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe
python -X utf8 tools/rend2/bake_model/compare_bakes.py --q3map2 $q3 --output build/x03-new-compiler
```

Compiler package/provenance and source-lock reproduction are documented in
[the existing bake fixture README](../tools/rend2/bake_model/README.md).
Frozen hash-check replay uses `build/bake-model-comparison-final`, whose BSPs match
the committed `validation.json`; a fresh bake's unversioned entity strings may
have a different BSP hash and must not silently replace those frozen inputs.

Prepare local licensed all-page X02 observations and sanitized outer-training
views using the [X02 workflow](rend2-observation-coverage-x02.md). Then:

```powershell
python -X utf8 tools/rend2/bake_model/run_x03.py --output build/x03-new-run --compiled-bakes build/bake-model-comparison-final --stock-root build/x02/final --ray-tool build/x03-cpu/lightrecon_profile_rays.exe
python -X utf8 tools/rend2/bake_model/report_x03.py --run build/x03-new-run/result.json --fresh-compiler-report build/x03-new-compiler/result.json --output build/x03-new-tables
```

On MSVC multi-config builds use `Release/lightrecon_profile_rays.exe`. Optional
`--compiled-bakes`/`--stock-root` stages may be omitted only with their absent
coverage explicitly reported (`null`). Tools never download compiler/game assets
or overwrite a previous run directory. The initial two-page data are not used
as a full-map coverage claim. This is a limited profile experiment on bounded
subsets, with source answers retained only by the controlling fixture harness.

## Acceptance results

Recorded 2026-10-27, branch `rend2-unified-wip`, starting revision `eb9ee6d1d`.
Both full runs produce exactly1,867,843 identical bytes, SHA256
`541d7d207c3b0ab6fd4582fd0a2714dedde4e8d3bbb4196d00322fa244468d9a`.
All35 locally inspected source files match the LF-normalized source lock.
The hypothesis catalogue is105,982 bytes. A full result contains parameter
alternatives and per-block diagnostics; the committed witness strips raw stock
observations and geometry. The scripts require no external Python dependencies.

### Spatial held-out results

Interval RMSE is in encoded RGB8 bytes; **zero means interval-consistent, not
unique**. The following pairs are validation / held-out. NR abbreviates the pinned
modern profile; L means linear/no-angle and IS means inverse-square/cosine.

| Synthetic scene and protocol | IS | L | Interpretation |
|---|---|---|---|
| Narrow distances, known amplitudes | 0 / 0 | 10.053 / 10.054 | Fixed source values distinguish these recipes |
| Same scene, fit RGB | 0 / 0 | .048 / .027 | Unknown amplitude nearly erases distinction |
| Same scene, fit height and RGB | 0 / 0 | 0 / 0 | Linear at z320 and IS at z144 both explain original z192 data |
| Broad distances, fit RGB | 0 / 0 | 13.254 / 9.558 | Distance diversity exposes curvature |
| Broad distances, fit height/RGB | 0 / 0 | 11.327 / 8.762 | Geometry freedom reduces wrong-law discrepancy |
| Several receiver normals, fit height/RGB | 0 / 0 | 11.326 / 11.438 | No-angle IS is also worse (2.739 / 3.056) |
| Door shadow, fit height/RGB | 0 / 0 | 5.387 / 6.796 | Correct height wins for IS in the bounded set |
| Fully clipped, fit RGB | 0 / 0 | 0 / 0 | All four falloff/angle combinations remain possible |
| Gamma2.2 targets, falsely lock gamma1, fit RGB | 20.080 / 24.330 | 15.844 / 27.798 | Wrong law can win conditional two-family validation |
| Same targets, common encoding candidate set | 0 / 0 | 6.583 / 5.322 | Correct transfer restores useful separation |

The four-way gamma comparison prefers **inverse-square/no-angle** if gamma is
wrongly locked, rather than recovering the actual cosine response. Spot targets
on a single floor have a linear/cosine alternative at0 /.011 byte RMSE, versus
the correct spot0 /0. Lexical tie order can list the wrong family first. Retained
alternatives, not the first row, are the meaningful result. Half-Lambert versus
cosine is also weak on this geometry: correct0 /0 versus .250 /.269 with fitted
RGB. Compiled fixtures below provide a different receiver distribution and do
distinguish several of these options more strongly.

Synthetic folds contain30/9/9 correlated samples for narrow/clipped scenes,
66/21/21 for the broad floor,108/36/36 for multi-surface scenes. Independent
patch counts are one third of those sample counts. Each compiled comparison
uses36/28/17 train/validation/held-out samples, with8 validation and6 held-out
spatial blocks. These are distinct surfaces/distances on the same known room,
not independent maps. Sensitivity to different train selections is reported.

### Compiled-fixture confusion/identifiability matrix

Held-out interval RMSE after fitting RGB, with known geometry and the fixture's
known output recipe. Columns all have the same three-amplitude budget. No row is
a stock source error or an exact historical revision classifier.

| True compiled recipe | IS/cos | L/no-angle | L/cos | IS/no-angle | IS/half-Lambert | Spot |
|---|---:|---:|---:|---:|---:|---:|
| inverse_square | .0159 | 11.6158 | 16.8830 | 3.8821 | .3898 | 18.8165 |
| linear | 13.3366 | .1480 | 11.0413 | 17.8093 | 13.9568 | 20.6892 |
| linear_angle | 13.3132 | 6.1965 | 0 | 14.7234 | 13.4717 | 1.3584 |
| no_angle | 4.7817 | 16.7024 | 23.4359 | 0 | 4.0706 | 26.1597 |
| half_lambert | .2524 | 12.2574 | 17.4457 | 3.2851 | 0 | 19.3619 |
| spot | 11.6602 | 4.7393 | 1.4955 | 12.8768 | 11.7973 | 0 |
| colour_gamma | .0017 | 33.1827 | 43.1369 | 5.3188 | .7257 | 54.7278 |
| colour_clip | 0 | 11.4684 | 21.7464 | 8.7788 | 1.6966 | 110.8271 |

With fixed known source amplitudes, every correct recipe is interval-exact0 /0.
For the inverse-square fixture, wrong linear held-out error is13.8044 fixed,
11.6158 with RGB fit,9.7661 with bounded height/RGB fit. On the linear fixture,
wrong IS similarly decreases from13.8837 to13.3366 to8.6144. Fitted correct
recipes need not be exact: the small search/quantization plateau is visible
(e.g. .148 byte for linear), and is not compiler discrepancy. NetRadiant,
GtkRadiant and original-q3map direct rules can coincide locally; these fixtures
cannot distinguish revision identity from shared formulas.

### Sensitivity table

This controlled two-family sweep fixes equal policies across IS and L. All rows
have IS truth; errors are held-out IS / L. Geometry uncertainty here means a
source-position prior error/free source height. It does **not** quantify errors
in the actual stock collision mesh or a general uncertain occluder shape.

| Change | IS / L held-out | Conditional outcome |
|---|---:|---|
| Baseline broad receivers | 0 / 9.611 | IS preferred |
| Narrow distance distribution | 0 / .027 | Both retained |
| Free source height | 0 / 8.762 | IS preferred, weaker separation |
| Wrong height prior288..320WU | 5.103 / 8.762 | Both become discrepant |
| Half the training patch blocks | .012 / 9.633 | Similar preference, numerical fit changes |
| Amplitude regularizer .01 | .019 / 9.611 | Stable family preference |
| Full clipping | 0 / 0 | No discrimination |
| Spatially varying unmodelled bounce | 10.131 / 22.485 | Nonzero model discrepancy |
| Same bounce plus bounded constant fill | 8.853 / 17.496 | Partial reduction, no GI replay |
| Wrong gamma locked | 24.331 / 28.383 | Validation prefers L despite worse held-out error |
| Common gamma/compensate candidates | 0 / 5.322 | IS preference restored |
| Door shadow and free source height | 0 / 6.796 | Shadow/configuration constraints remain useful |
| Multiple receiver normals | 0 / 11.438 | Stronger held-out separation |
| Unknown count, complexity weight0/.25/4 | 3.260 / 11.399 in each run | Count selection unchanged in this finite sweep |

Two-source truth with only a short coordinate search illustrates an optimization
failure: a one-source midpoint can be chosen over the correct two-source
configuration because its training minimum is found more easily. Even fixed
two-source geometry/RGB fitting has1.864 held-out error after two passes, whereas
the known answer is exact. This is **not** proof that the scene is fundamentally
unidentifiable. Constant-fill fits also remain imperfect after the bounded search.
Source-count selection requires convergence/multiple-start checks before any
physical inference. The 0/.25/4 sweep is a reported sensitivity result, not a
calibration of the complexity penalty.

For the broad IS-versus-L validation blocks, paired resampling of the MSE
difference gives mean -175.668 byte²,5/95 percentiles -244.751/-130.134 over7
blocks. These are first-versus-second catalogue candidates, not necessarily the
top-ranked two models in every experiment. Small block counts, spatial dependence
and unknown model discrepancy prevent interpreting the values as stock compiler
probabilities.

The analytic tangent example has RMSE .00409 over r238..242, versus41.299 over
r96..480 (compiler-relative scalar domain, not encoded-byte RMSE). The narrow
distance/intensity gauge is .0597 versus16.3008 on a broad receiver patch.
Albedo/intensity and gamma/compensate gauges have zero error to floating-point
precision. These known counterexamples explain why a single RMSE threshold is
not an identification criterion.

### Stock evidence: explicit absence of identification

| Stage | hoth3 | hoth2 outdoor ROI |
|---|---:|---:|
| All X02 outer-training surface observations | 125,944 | 14,154 |
| In selected ROI | 4,110 | 0 |
| Planar with matching geometric/shading normals | 4,027 | 0 |
| Excluded at nested fold support gaps | 1,891 | 0 |
| Remaining nested train/validation/held-out | 1,206 /479 /451 | 0 /0 /0 |
| Geometry-only spatially thinned/capped | 48 /44 /48 | 0 /0 /0 |
| Common certified rays for both source hypotheses | 9 /8 /8 | 0 /0 /0 |
| Common visible rays | 0 /0 /0 | unavailable |
| Profile ranking | **Unavailable: all blocked** | **Unavailable: no training observations in ROI** |

hoth3:280 individual receiver/source rays,228 unknown,52 blocked,0 visible.
The common certified subset has25 receivers; there are two additional receivers
with only one certified source ray. Scene completeness remains false. An
exploratory set of five fixed room-centre hypotheses also had only one visible
ray out of700,595 unknown and104 blocked; it was not admitted as a profile-ranking
benchmark. This is a coverage/source-hypothesis limitation, not a falloff victory.

Map SHA256: hoth3
`821e6413f10e1e4aca7b6a5fd1bc57a6731bce5d9075cb0175208915cee5cb32`;
hoth2 `3fc5d0a469c7246d09cb9d0be4500fc0f9a5923ca9a23fdee51aba9eceb3f71d`.
No physical XYZ, candela, compiler error or stock-profile confidence is reported.
An informative stock attenuation ranking and an outdoor sun/sky ranking remain
unverified. Further trials need a predeclared region with genuinely exposed
receivers, adequate spatial folds and an evaluable sky/transport model; opening
held-out files or treating unknown rays as transparent is not a remedy.

### Tests/builds actually executed

* `python -X utf8 tools/rend2/bake_model/test_kernels.py -v`:7 passed.
* `python -X utf8 tools/rend2/bake_model/test_identifiability.py -v`:11 passed.
  Covers feature evidence, unsupported/invalid inputs, interval censoring,
  compiler encoding, batch/public kernel equality, linear cutoff reach/search,
  exact gauges, split gaps, fit/selection isolation and deterministic repeats.
* `compare_bakes.py --q3map2 <pinned executable> --output build/x03/compiler-recheck
  --case inverse_square --case linear`:2 compiled cases passed.
* `compare_bakes.py --q3map2 <same executable> --output build/x03/compiler-all22`:
  22 fresh compiled cases passed,66 compiler invocations. Binary SHA256 remains
  `894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4`.
* Two complete `run_x03.py` runs, replacing the example output paths with
  `build/x03/accepted` and `build/x03/accepted-repeat`:byte-identical reports;
  14 synthetic scenes,16 sensitivity cells,22 original BSP replays,8 three-protocol
  compiled comparisons and the explicitly non-identifying stock trials.
* `cmake --build build/lightrecon-disk-standalone --target lightrecon_bake_profile_test
  lightrecon_profile_rays --parallel 2`, then
  `ctest --test-dir build/lightrecon-disk-standalone --output-on-failure -j 2`:
  GCC17/17 passed. Header-only contract changes subsequently rebuilt/retested
  with `-R lightrecon_bake_profile`.
* `cmake --build build/lightrecon-core-renderers --config Release --target
  rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_bake_profile_test
  lightrecon_profile_rays --parallel 2`:SP/MP Release passed.
  `ctest --test-dir build/lightrecon-core-renderers -C Release
  --output-on-failure -j 2`:MSVC17/17 passed; focused contract retest after final
  header changes. Existing Git-tag/CPack and MSVC x64 `/arch:SSE2` warnings remain.
* `python -X utf8 tools/rend2/lightrecon_regression.py --no-gpu
  --output build/x03/shader-contracts`:27 programs/51 stages,zero differences,
  production dependency audit passed. This is a preprocessing/binding contract
  comparison; GPU compilation and driver reflection were **not** run.

Early exploratory runs are retained under `build/x03/experiment-v1`,
`experiment-final`, `experiment-repeat`, `stock`, `stock-nested`; they are not the
accepted witness. Two issues were corrected before acceptance: a coarse search
could miss a narrow lit minimum behind a linear cutoff's dark plateau (fixed
subdivision and regression test), and blocked-only stock fits could appear in a
lexically sorted tied ranking (now explicit `NoDirectResponse`, no ranking).
An initially over-strict .1-byte cutoff-search assertion failed on the genuine
.376-byte mismatch between two different laws; the test now requires <1 byte
instead of incorrectly requiring exact equality. Existing renderer kernels and
the seven original kernel tests were not changed.

No gameplay visual validation was performed or claimed for this numerical-only
change. No renderer consumer, shader, cvar, PBR calibration, original lightmap
upload, Ghoul2, Forward+, fog or sun-shadow implementation was modified.
