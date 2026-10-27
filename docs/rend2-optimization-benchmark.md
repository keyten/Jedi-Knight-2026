# Numerical optimization blocks for inverse baked lighting

Research/benchmark date: 2026-10-27. Repository baseline `b34ff011f` plus the
evaluation-framework work in this chat. No production inverse solver, emitter
publication, renderer integration, shader or cvar change is introduced.

Executable: [optimization tools](../tools/rend2/optimization/README.md).
Measured results: [tables](../tools/rend2/optimization/results/tables.md),
[numerical witness](../tools/rend2/optimization/results/result.json),
[resource/validation record](../tools/rend2/optimization/results/validation.json).
Performance values are local CPU measurements, not a full-map time prediction.

## 1. Decision and scope

Use separate numerical methods for separate mathematical problems:

| Block | First implementation recommendation | What it does not establish |
|---|---|---|
| A: genuinely linear nonnegative RGB amplitudes | Small dense NNLS/BVLS reference; normalized-column coordinate/block updates with warm starts for repeated larger subproblems. Check KKT and rank. | Unique sources, valid calibration or correctness of a nonlinear compiler approximation. |
| B: uncertain bake profile | Discrete historically supported hypotheses; identical nuisance bounds/restarts; fit each independently, rank on validation, preserve alternatives. | Historical Raven identity or posterior profile probability. |
| C: stable-visibility continuous geometry | Scaled box-constrained trust-region least squares; use verified derivatives where available. Damped GN is a useful small reference; L-BFGS-B can polish stable branches. | Geometry observability, source existence or applicability across trace/type changes. |
| D: changing shadow topology | Geometry-aware initialization/discrete basins, then footprint-integrated refinement. Derivative-free bounded search/restarts can cross plateaus. Strict-reference reevaluation is mandatory. | A sampled mask's derivative becoming physically correct because the optimizer terminates. |
| E: source count/type | Bounded activation masks / type proposals, group sparsity for screening, unpenalized refit and validation/complexity comparison. | Existence probabilities or original authoring count for exactly equivalent sources. |
| F: direct/indirect | Alternate direct amplitudes and a passive bounded transport model; preserve unresolved discrepancy and profile/material alternatives. | Free residual RGB being physically recovered GI. |

These are recommendations for the **next solver design**, supported by bounded
experiments here. They are not a production pipeline assembled from these scripts.
TRF is a safe initial default for a smooth parameter block, not a universal winner;
the spot/visibility controls intentionally retain failures and flat minima.

Use the existing [thesis analysis](rend2-inverse-lighting-research.md),
[bake model](rend2-bake-model.md), [localization](rend2-light-localization-research.md),
[X03](rend2-bake-identifiability-x03.md), [X04](rend2-shadow-footprints-x04.md),
[GI attribution](rend2-inverse-gi-research.md) and
[evaluation contract](rend2-lighting-evaluation.md). No generic literature survey,
new observation decoder or compiler investigation replaces that evidence.

## 2. Actual inputs and mathematical domains

### 2.1 Compiler-backed observations

The runner content-checks the original NetRadiant `d9338309e` BSPs against
`bake_model/validation.json` and reruns the existing scalar/grid/deluxe verification
for inverse-square, linear, spot and colour-clipping cases. These are **existing
unmodified compiled files**, not a fresh compiler invocation. The numerical
functions are the already verified `point_scalar`, `spot_factor` and
`continuous_encode`, with the original compiler gauge:

```text
I = max(Qrgb), colour = Qrgb / I
preencoded_rgb = sum_sources colour * compiler_scalar(I, geometry, profile)
predicted_bytes_continuous = ColorToBytes_without_integer_cast(preencoded_rgb)
```

Zero intensity is handled separately. Source parameters retain **compiler-relative
units**, never relabelled candela, nits or lux. All profiles get the same admissible
RGB parameters; different attenuation laws are not blended into one differentiable
"compiler" parameter. Original stock JA remains UnknownProfile.

The multiple-source `two_colour` and shadowed `door_occlusion` cases use existing
X02 training/validation/held-out exports and their separate authored recipes.
Recipe/BSP hashes must agree. The existing standalone `lightrecon_profile_rays`
queries the sanitized static visual/collision geometry at the fixed candidate
origins. Unknown rays exclude a common observation from these fitting comparisons,
with explicit counts. They never become transparent. Query preparation costs and
all query hashes/counts are recorded separately from repeated amplitude fitting.

The four scalar fixtures are known convex rooms. Their prescribed source boxes
remain inside the room, so source motion there has certified unchanged visibility
under that controlled geometry. This is not an assertion about arbitrary BSP areas,
PVS, stock alpha surfaces or moving doors.

### 2.2 Synthetic/reference objectives

Generated continuous experiments use the verified direct kernels on explicit
multi-surface receiver geometry. They compare XYZ, XYZ plus intensity, spot-axis/
radius, and finite rectangle dimensions. The rectangle reference is deterministic
8×8 source quadrature, fixed for every compared optimizer; it is **not** q3map's
surface-emitter subdivision/sampling replay. A far-area plus unknown luminance
control exposes intensity/area ambiguity.

The changing-shadow experiments use X04's existing exact planar solid-angle
integral, projected polygon reference, fixed samples and adaptive approximation.
They are SI/reference controls, not a substitution for an unidentified historical
luxel operator. The GI experiment uses the existing passive transport helper with
an explicit small directed network; it is not a new bounce renderer.

Algebraic near-collinear matrices, zero columns and extreme scaling isolate
numerical behaviour. They are labelled controls, not compiled-map recovery.

### 2.3 Training and hidden targets

Only training rows/targets enter each `Objective`. Bounds and fixed priors follow
the declared experiment. The controlling runner evaluates validation/held-out
after a method returns. Source truth is used for error measurement and the
explicitly named known-geometry/intensity protocols, never secretly supplied to
an unknown-parameter block. Existing footprint-separated compiler folds are reused.

This is a trusted in-process benchmark, not an OS sandbox for hostile code. Use
the separately tested [blind input boundary](rend2-lighting-evaluation.md#11-blind-access-boundary)
for future third-party solvers. Full benchmark outputs, recipes and truth-bearing
scripts must remain outside solver mounts. No held-out RGB selects a learning rate,
iteration stop or source mask in these experiments.

## 3. A: linear amplitudes, sparse blocks and conditioning

For fixed geometry and a genuinely linear observation operator:

```text
min_X>=0 0.5 || W^(1/2) (A X - Y) ||_F²
```

X has one RGB row per source. Compare SciPy NNLS, bounded-variable least squares
(BVLS), linear trust-region reflective least squares, projected RGB coordinate
updates and group-proximal iterations. Columns are normalized using weighted
response norms; coefficients are converted back before error measurement. This
makes source-group penalties response-scaled rather than summing unrelated raw
cd/nits/lux values.

Reference optimality conditions are X>=0, gradient>=0 on zero coordinates and
gradient≈0 on positive coordinates. Report maximum KKT violation, singular values,
rank and condition separately from residual. Near-duplicate candidates can give
excellent predictions with unstable source allocations. A rank-deficient response
is not automatically a numerical routine failure; condition is explicitly
unavailable/infinite, not zero.

Coordinate updates use the full RGB row and maintain the current prediction.
Group proximal updates penalize `lambda * sum_j ||X_j||2`, with nonnegative
projection; penalties and their objective bias are recorded. Unpenalized KKT
residuals are not falsely applied to the penalized model. Strong sparsity may
discard a real source; weak sparsity may retain redundant correlated sources.

**Compiler restriction:** inverse-square is linear in RGB only on a fixed active
branch. The compiler's fast envelope/threshold still depends on peak intensity.
The linear-block comparison excludes peak-censored training samples, uses the
known gamma=1 encoding and records the admitted count and strict-kernel discrepancy.
Quantized RGB is a noisy/interval target, not exact incident irradiance. Final
validation uses the full compiler encoding/cutoff even after an approximate NNLS
seed. NNLS must not silently become the global solver for all compiler observations.

In particular linear attenuation is

```text
colour * max(angle * 7500*I/8000 - distance*fade, 0)
```

with envelope/threshold rules, **not** `unknown_gain * precomputed_light_column`.
Unknown RGB couples through `I=max(Q)` and the distance offset. Those cases use
the exact nonlinear operator in block B; blindly constructing a column at I=1
would create the wrong inverse problem.

The ill-conditioned controls compare two co-located columns, nearly proportional
columns and a zero column. Float32-*input* perturbations to the float64 NNLS
adapter are labelled as such. A separate NumPy least-squares diagnostic actually
retains float32/float64 arithmetic and permits signed coefficients. It is not
clipped and advertised as a valid nonnegative emitter solution.

## 4. B: discrete profile and photometric hypotheses

Known-source geometry trials fit the same 3 RGB parameters per source for
inverse-square, linear and spot families. Each gets starts at compiler amplitudes
80, 240 and 600 and the same forward budget. Dark linear plateaus and bright
encoding plateaus are retained as numerical outcomes. Work spent on losing starts
and wrong profiles remains in the report.

Training objective chooses a restart within a hypothesis. Validation compares
profile hypotheses; held-out is a final check. A lower residual is conditional
evidence, not verified historical identity. Different profile feature support and
unmodelled trace/filter/material effects remain discrepancy. Do not optimize a
continuous blend between different historical rules and publish the blend as a
recovered compiler.

Unknown global gain, per-source RGB, region ambient, arbitrary GI, attenuation
parameters and source count are not all free simultaneously. The individual
experiments freeze all but their declared block; colour and GI controls explicitly
measure the consequence of relaxing selected nuisance parameters.

## 5. C: geometry, directions and finite dimensions

Most methods share scaled box coordinates u:

```text
p = lower + u * (upper-lower), 0 <= u <= 1
```

The same data, loss, parameter bounds and starting points are supplied to:

- SciPy bounded TRF nonlinear least squares.
- A small educational projected damped Gauss–Newton/LM loop with trial acceptance.
- SciPy L-BFGS-B using the same residual/Jacobian and scalar objective.
- Projected Adam with fixed betas .9/.999, rate .025 and epsilon 1e-8.
- Bounded Powell search without derivatives.

TRF is the maintained reference implementation. The local damped-GN loop is
tested against known solutions and derivatives, but not offered as a new numerical
library. It is called `projected_lm` to distinguish its projected bound handling
from unconstrained MINPACK LM. Bounds remain identical across methods.

Generated smooth XYZ targets test well-conditioned multiple normals, a single
plane, a tiny distance baseline with unknown intensity, and XYZ plus amplitude.
The plane alone does not imply an exact universal ambiguity: spatially broad
sampling of a known point model can still constrain position. The narrow-baseline
and scaling controls explicitly measure weaker directions instead of assuming
every planar fit has the same rank.

The generated `C/distance_ambiguity` control deliberately still includes the
independent wall receiver family, so it becomes identifiable in the noiseless
case. A separate [receiver-baseline ablation](../tools/rend2/optimization/results/distance.json)
removes that wall and compares one repeated point, a narrow plane, a broad plane
and multiple surfaces using the same height/intensity parameters. It has 96
additional fits (four methods, two starts, three seed/noise combinations). The
one-point Jacobian is rank deficient; at truth the narrow-plane condition is
about 5,958, broad-plane 6.69 and multi-surface 5.57 in declared unit-box scales.
One narrow-plane TRF start retains about 84.5 WU height error with tiny residual.
This demonstrates why names/pixel count alone cannot certify information content.

Spot axis is normalized from `(tx,ty,-1)` within a local orientation chart. Radius
is positive and maps through the actual compiler cone rule, not a replacement
smoothstep. Core-only receivers have no axis/cone information when the response
is clamped fully on. A zero residual there with a wrong cone is an honest
non-identifiability result. Edge receivers improve information, but hard spot
thresholds introduce branch-local minima even with unchanged geometric visibility.

Rectangle half-dimensions remain strictly positive; the far-field control also
varies luminance. Report position errors in world/metres, axis/cone errors in
degrees, dimension-relative error, and dimensionless scaled parameter error.
Do not combine world position and intensity into a supposedly physical XYZ error.
Scaled response singular values are diagnostic, not calibrated Fisher information.

Separate trials refine XYZ against **actual compiled** inverse-square, spot and
colour-clipped RGB with known profile/intensity. These report interval error,
source-position error and failures; generated exact-kernel success is not
substituted for the compiled result.

## 6. D: moving shadow boundaries

For source position theta and receiver footprint A, the reference is

```text
mean_A [ V(x,theta) * geometric_response(x,theta) ]
```

not centre visibility multiplied by a mean response. X04's closed-form rectangle
solid angle includes the derivative of the moving integration limit. At the
chosen edge fixture, reference visibility has height derivative 1.111111..., while
five fixed point samples have derivative zero for sufficiently small steps.
Differentiating only smooth falloff with a frozen binary mask misses the boundary
term. Determinism or automatic differentiation does not repair that omission.

The benchmark compares exact reference, centre ray, 4×4 fixed points, point-query
adaptive quadrature, boundary-aware polygon integration and a deliberately smooth
visibility surrogate. Every evaluator has its own result group; medians are never
pooled across different training approximations. All final source candidates are
scored on the stricter reference integral at separate held-out footprints.

Central differences use height steps 1e-4, .001, .01 and .1 m and compare to the
independent reference derivative. A small step can see no binary-mask event; a
large step can average multiple topology changes. Report gradient and value error,
not merely whether finite differences match the method's own sampled function.
Adaptive point samples can alias a narrow edge; their local error estimate is not
a completeness proof. The polygon method uses explicit projected boundary pieces
plus smooth numerical integration, with actual response/projection counts.

There are fully blocked and fully visible starting basins. A local gradient can
legitimately vanish before reaching informative evidence. Use room/fixture and
caster-boundary initialization, bounded discrete position proposals, derivative-
free search or restarts; do not retry Adam/L-BFGS indefinitely and call the final
plateau a physically recovered lamp.

The sigmoid surrogate width is **.025 m for this optimization experiment only**.
It does not modify targets, define a universal penumbra, replace the reference
value or explain q3map's 35 boundary texels. The prior X04 compiler boundary
diagnostic remains separate and unchanged.

## 7. Losses, colour and independent observation operators

### Quantization and clipping

For original compiler truncation, ordinary bytes b give a pre-integer encoded
interval [b,b+1); 255 is lower-bound information. The optimization uses the
conservative closed-end relaxation. Black channels retain [0,1), not an invented
positive light. A signed interval residual is zero inside an admissible interval
and otherwise distance to its closest endpoint.

The entire profile's output operator precedes comparison. q3map's shared-peak
normalization couples RGB and can lose absolute scale; it is **not** three
independent channel clamps. `colour_clip` uses that actual operator. A separate
explicit per-channel LDR fixture compares naive squared error against censored
intervals in its own source-linear domain. Values above its clipping threshold
are not penalized solely for exceeding a saturated byte. This does not assert the
same preimage for every historical compiler.

Unknown transfer or optical rules return UnspecifiedModel/Unavailable. No loss
silently maps unknown historical bytes to physical lux. Lightgrid ambient/directed
would need separate operators; this benchmark does not invent their equivalence
to surface incident irradiance or direct/indirect components.

### Robust errors

Controlled outlier tests compare L2, Huber and Cauchy on identical heterogeneously
weighted observations/noise seeds. Huber/Cauchy reduce unmodelled outlier influence,
but neither supplies missing saturation information nor justifies changing a
known censoring operator. Robust scales are recorded in normalized residual units.
A small RGB loss may still reflect the wrong geometry/profile.

### Colour parameterization

Independent RGB has six unknowns for the two-source control. Known chromaticity
reduces this to two nonnegative amplitudes; a wrong white prior has the same
smaller budget but biases predictions and colours. Report prediction error,
chromaticity error and parameter count. A trusted authored colour can reduce
variance; an uncalibrated glow texture is not automatically such a constraint.
Do not optimize both a free RGB scale and a redundant intensity gain.

## 8. E and F: count/type and indirect blocks

The count control supplies four fixed candidates, including a near-duplicate,
before target fitting. It evaluates all 15 nonempty activation subsets and four
group-sparse strengths. Each subset is refit and validation error plus an explicit
complexity cost selects the winner. Held-out evaluates the frozen selection.
All unsuccessful masks consume reported work. Source precision/recall uses the
existing identity-independent evaluation module. Sparsity coefficients are
fixture-domain diagnostic settings, not calibrated emitter-existence probabilities.

Point versus spot is a discrete type comparison at the same supplied geometry.
It is not an interpolation between emitter kinds. This bounded experiment does
not add production candidates, discover arbitrary counts/poses, fit tubes/sun/sky
or claim that source-count priors identify exactly co-located original emitters.

The indirect control uses a passive area-aware transfer network with bounded
reflectance and no connection through its sealed wall. With correct direct model
and reflectance, fitting the source-linked response preserves the known source.
Alternating direct intensity and a bounded transfer fraction tests convergence
from two initial fractions. Wrong direct profiles or reflectance still bias source
intensity and attribution even when an update converges.

A deliberately free per-observation residual plus source sparsity collapses the
true direct intensity to zero while explaining training RGB. Its output is labelled
unresolved, not physical GI. Physical transport, sky, artist ambient and signed
direct-model discrepancy must remain separate. Unknown optical paths cannot be
filled in as energy-carrying transparent connections just to reduce the loss.

## 9. Sampling, schedules and work accounting

Receiver subset controls compare uniform, area-weighted, spatially stratified,
normal-diverse, region-local and saturation-aware subsets. The area-weighted
without-replacement subset is a sampling heuristic, **not an unbiased full-map
integral estimator**; subset weights and common held-out targets remain explicit.
A separate candidate-boundary sampling control tests shadow information. Its
known candidate geometry is stated, not presented as oracle-free deployment
localization. Region-local subsets can remove important receiver baselines.

Stochastic Adam draws seeded batches and freezes batch indices for every finite-
difference gradient calculation (common random numbers). Best checkpoints use
full **training** loss, not held-out targets. Report unique receivers and actual
response work; batch steps are not equivalent to full-data steps. Raw historical
lightgrid cells remain immutable and are not relocated or smoothed to improve loss.

Warm-start coordinate solves reuse amplitudes after each new matrix is rebuilt.
Coarse→fine and alternating coordinate schedules are separate experiments, with
the sum of stage budgets visible. A source count/type/profile/visibility change
requires history reset and dependent-cache invalidation. Parameters may be a
warm start; obsolete light contributions may not.

Each nonlinear run records forward calls, derivative calls, scalar responses,
actual ray queries, optical visibility tests and polygon projections. Finite-
difference calls count as forward calls even when a library's `nfev` omits them.
Dense library-internal BLAS matvec counts are **unavailable**, not fabricated;
matrix dimensions, iterations and actual wall time are reported instead. Actual
fixed-geometry BSP ray preparation is recorded separately from optimizer calls.

Comparisons share per-problem data, constraints, starts, 700 forward evaluations
and a 200,000 scalar/ray/projection cap (checked between whole evaluations; one
bounded batch may cross the cap). Visibility trials have a smaller common 180
forward cap. These are work-budget experiments in addition to numerical stopping,
not equal GPU/frame budgets. CPU time and strict held-out quality must accompany
iteration counts; fewer iterations with many more rays is not a speed result.
No timings are pooled to rank unrelated objectives/evaluators.

The [per-method resource matrix](../tools/rend2/optimization/results/resources.json)
uses a new process for each of five linear and five nonlinear algorithms plus an
import-only process. On these small cases measured peaks are about 67–68 MiB,
including Python/NumPy/SciPy; this is not an array-capacity estimate. Native import
cost dominates, so these measurements do not establish large sparse/full-map memory
scaling. Exact argv, process wall time, solve wall time, work and numerical statuses
are retained. Peak differences are not treated as exact incremental allocations.

## 10. Constraints, safeguards and stopping

Parameters use finite boxes: XYZ inside the controlled room, positive rectangle
dimensions, bounded local spot chart/radius, nonnegative RGB. Coincident receiver/
source positions are rejected; the known compiler's 16-WU distance rule remains
its own convention. No artificial universal physical near-light falloff is added.

Synthetic one-dimensional solid/fixture/recess controls compare hard exclusion
against a soft prior. A soft prior alone can enter a known solid and is not a
geometry certificate. Unknown region membership does not disable known solid
constraints. These are policy diagnostics, not another BSP classifier; use the
existing CPU geometry/visibility layer for actual source extents and rays. A
continuous proposal crossing a wall needs swept/feasible-path handling or an
explicit discrete change of basin; endpoint-only acceptance is insufficient for
that production decision.

Reference numeric settings:

- Float64 parameters/residuals/Jacobians; local unit-box coordinates.
- TRF: ftol=1e-10, xtol=1e-9, gtol=1e-8; explicit forward budget includes FD work.
- L-BFGS-B: ftol=1e-12, projected-gradient gtol=1e-8, max line-search steps 20;
  reset curvature pairs after a model/visibility change.
- Damped GN: initial lambda=1e-3, bounded trust step .25 in unit-box coordinates;
  reject non-improving trials, shrink trust region, increase damping.
- Central FD: 1e-5 of each parameter span for optimization, separate multi-step
  derivative validation. Do not use complex-step differentiation through min/max,
  clipping, normalized peak selection or binary visibility.
- Group/coordinate loops have explicit sweep/matvec caps, residual/rank/KKT
  diagnostics; a tiny parameter change alone is not proof of a unique fit.

Statuses distinguish Stationary, Stagnated, IterationLimit, WorkLimit, Cancelled,
infeasible input and invalid/nonfinite values. Best feasible evaluated parameters
are retained for diagnostic restart after budget/cancellation; such a partial
result is not a publishable reconstructed asset. Library stationarity is never
translated into physical confidence. Rank deficiency is an orthogonal information
diagnostic. Unknown profile/geometry is not numerical convergence to zero light.

For production stopping, combine scaled projected gradient/KKT, relative objective
change, parameter motion in physically meaningful units, numerical stability and
controller-owned validation checkpoints. If validation plateaus while train improves,
stop/increase evidence scrutiny rather than open the final holdout. A stalled
non-smooth/zero-response basin needs a new proposal, not a smaller tolerance.
Final held-out targets never select the stopping rule.

## 11. Proposed high-quality offline schedule

1. Validate observation readiness, domains, geometry and supported profile rules.
   Preserve missing/unknown regions; valid direct queries need no invented room ID.
2. Enumerate a small historically justified profile family. Freeze comparable
   nuisance budgets, source candidate pool, calibration gauge and evaluation folds.
3. Score supplied geometric/fixture candidates cheaply; inspect response norms,
   zero columns and correlated groups. Do not turn bright reflected transport into
   a new physical source.
4. Fit nonnegative RGB with NNLS/BVLS only where the forward block is genuinely
   linear. Otherwise use the exact profile operator, interval loss and intensity
   bracketing/restarts across emission cutoffs. Warm-start stable blocks.
5. Use scaled trust-region local geometry refinement within certified feasible
   basins, with conditional amplitude refits. Check branch/cache validity after
   every accepted source pose/extent change.
6. Refine promising shadow evidence with footprint integration and geometry-aware
   boundary proposals. Reevaluate rays after position, extent, orientation,
   occluder policy or geometry generation changes. Fixed-geometry amplitude-only
   updates may reuse valid factors/masks.
7. Propose bounded source additions/removals/types; refit comparable blocks and
   penalize complexity. Record all work, including rejected hypotheses.
8. Alternate only constrained source-linked indirect transport where materials and
   geometry support it. Keep model discrepancy/unresolved fields separate and
   monitor direct-source recall/energy suppression.
9. Reevaluate every finalist with the stricter physical/bake reference, required
   output encoding and held-out protocol. Approximation/surrogate improvements do
   not excuse strict-reference errors or wall leaks.
10. Preserve competing profiles/source counts and assess profile likelihood /
    feasible sets with parameter/material/visibility uncertainty. Local Hessian
    rank or restart frequency alone is not existence probability.

This schedule deliberately accepts expensive offline refinement and cacheable
results, without putting optimization inside map load or enabling any consumer.

## 12. Dependencies, licensing and remaining work

NumPy **2.3.5**, SciPy **1.16.3**, Python **3.12.14**; research-only BSD-3-Clause
libraries with additional wheel-bundled library notices. Exact local wrapper/native
and license hashes are in [dependencies.json](../tools/rend2/optimization/dependencies.json).
The original wheel was not retained, so no verified wheel checksum is claimed.
No third-party implementation/binary is committed or linked to SP/MP. No large AD
framework, GPU optimizer or new renderer dependency is introduced.

Relevant maintained implementations inspected:
[NNLS](https://github.com/scipy/scipy/blob/v1.16.3/scipy/optimize/_nnls.py),
[bounded linear LS](https://github.com/scipy/scipy/blob/v1.16.3/scipy/optimize/_lsq/lsq_linear.py),
[nonlinear least squares](https://github.com/scipy/scipy/blob/v1.16.3/scipy/optimize/_lsq/least_squares.py),
[SciPy license](https://github.com/scipy/scipy/blob/v1.16.3/LICENSE.txt) and
[NumPy license](https://github.com/numpy/numpy/blob/v2.3.5/LICENSE.txt).
SciPy's NNLS is an active-set constrained solve; BVLS/TRF support bounded linear
problems; nonlinear TRF supports bounds and robust residual losses. Actual installed
1.16.3 signatures/source hashes were checked rather than assuming a rolling API.

Unverified/unsupported: arbitrary stock geometry/material fitting, learned or exact
multi-bounce GI, general source discovery, full orientation manifolds, tube/sun/sky
nonlinear blocks, large sparse/distributed performance, exact compiler tracing at
all shadow boundaries, calibrated source-existence posteriors and production cache
publication. Those require the existing quality/coverage framework and additional
controlled data, not an optimizer switch. SP/MP builds are not required by this
research-only Python change and are not newly claimed.

## 13. Measured findings and engineering decisions

The validated full run contains **620 per-method/start results**, plus 96 separate
receiver-baseline fits. It took **153.77 s**, with **81,612,800 bytes** peak OS
working set (77.83 MiB). Timing includes fixtures, strict kernel checks, fixed
geometry ray preparation and report generation. It is not full-map reconstruction
time. Repeated-run hashes and measurements are retained in the validation record.

Selected results, with domains kept separate:

| Experiment | Method / comparison | Quality and cost | Consequence |
|---|---|---|---|
| Actual compiled two-colour, fixed geometry | NNLS | Held-out interval RMSE .10490 RGB8 units; relative RGB-parameter error .975%; median dense solve .00029 s. | Useful cheap seed on the supported linear branch; quantization/profile discrepancy still exists. |
| Same two-colour block | BVLS / TRF-linear / coordinate | Essentially identical residual and parameters; timings differ at sub-millisecond scale. | Do not declare a universal speed winner from tiny matrices. Repeated warm solves justify measuring coordinate methods at scale. |
| Actual compiled linear attenuation | Exact linear profile versus isq countermodel | Median held-out interval RMSE .10892 versus 13.3366. | Correct discrete model matters more than substituting optimizer names. Dark initial states can remain locally stationary. |
| Actual compiled point XYZ, known intensity | TRF / L-BFGS-B / Powell / Adam | Position errors .02149 / .02037 / .01350 / .10250 m; forward medians 56.5 / 126 / 335 / 700. | TRF is an efficient initial local block. Powell improves this quantized result at greater work; Adam does not win this budget. |
| Actual compiled spot XYZ | TRF / Adam | Held-out interval RMSE .11428 / .04909, forwards 63.5 / 381.5. | Adam can be useful, but the gain is not free and does not generalize to cone identifiability. |
| Moving shadow, exact footprint | TRF and projected GN | Some starts stop at wrong constant-visibility basins; median position error 1.28 m. | A correct forward value does not eliminate local minima or establish localization on a flat branch. |
| Moving shadow, surrogate then strict TRF | Same total 180-forward allowance | All three starts strict-refine; median error 5.0e-12 m, median 51 forwards. | Promising initialization schedule on this analytic scene, not a universal shadow-width model. |
| Moving shadow, fixed 4×4 versus point-adaptive | Bounded Powell | Median position error .1229 versus .01318 m; about 2,176 versus 21,216 actual rays. | Better approximation costs substantially more visibility work; iteration count is insufficient. |
| Independent-channel clipped fixture | Naive L2 / censored interval | True amplitude 2; fitted .71895 / 1.99513. | Robust loss cannot replace a correct censored observation operator. This is separate from compiler shared-peak saturation. |
| Outlier fixture | L2 / Huber / Cauchy | Median clean held-out RMSE .23325 / .01714 / .00233. | Robustness helps the declared outlier process, not unknown calibration or missing physics. |
| RGB parameterization | Independent / known chroma / wrong white prior | Held-out RMSE .19149 / .07155 / 16.9669. | Trustworthy colour priors reduce degrees of freedom; wrong priors can dominate optimizer error. |
| Near-proportional columns | Condition ≈4.62e8 | Coordinate allocation error ≈2.84 despite tiny prediction residual; float32-input rounding changes allocations. | Diagnose ambiguity/correlation and merge proposals rather than interpreting stable RGB as exact sources. |
| Count selection | Bounded subset refit versus group sparsity alone | Selected true candidates [0,1]; held-out error ≈4.45e-15. All tested group strengths also retain near-duplicate candidate 2, precision 2/3. | Group sparsity is screening, not final source-count certification. |
| Direct/indirect | Passive source-linked transport / flexible residual | Passive correct model recovers intensity 3; flexible residual drives intensity to 0. | Restrict transport and preserve unresolved discrepancy; a perfect residual fit can erase the real lamp. |

The two real geometry-backed datasets are not empty: `two_colour` has 332 initial
ray queries, 296 visible retained rays and 18 observations excluded for unknown
queries; `door_occlusion` has 166 initial queries, 85 visible and **63 blocked**
retained rays, also 18 excluded observations. Each retains train/validation/held-out
counts **58/42/48**. Their assumed +1-WU normal push is documented model scope,
not a newly verified arbitrary-surface compiler sampling replay.

Sampling did not produce one universal winner: the small area-weighted and
region-local subsets predicted better than the tested uniform subset, while the
normal-diversity prefix did worse. The deliberately local candidate-boundary
selection can itself land on an uninformative basin. Broad geometric support and
held-out checks matter more than attaching an informative-sounding selector name.
The short stochastic Adam schedule used 420 counted forwards and did not match
the fully converged deterministic local block's position accuracy.

On the near-collinear signed-LS diagnostic, float32 arithmetic gives coefficient
error .00764 versus about 1.85e-9 for float64. This motivates double/local scaling;
it is not a claim that FP32 is unusable for every field representation. Raw-unit
Adam and projected-GN controls are far worse than their scaled counterparts; the
maintained TRF solve handles this small example more robustly. A coarse/fine
schedule uses 42 total forward calls versus 35 for the full solve here, so no
speedup is claimed from this compact example alone.

No method result supports a calibrated physical-source confidence claim by itself.
Before adoption, expand verified spot-edge/material profiles, noise/discrepancy
models, unknown-occluder cases, large sparse candidates and real region support.
Retain the 30 new conformance tests and the separate evaluation, X04, GI and CPU
regressions as prerequisites, not stock-map recovery certification.
