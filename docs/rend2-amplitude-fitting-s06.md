# S06: fixed-geometry non-negative compiler-relative RGB fitting

This is a CPU-only numerical stage, not source discovery or a completed physical
reconstruction. It changes no XYZ, source count/type, renderer light list,
lightmap upload, cvar, fog, shadow resource or gameplay lighting. A fixed source
configuration and an explicit supported bake hypothesis are inputs. Stock JA
remains `UnknownProfile`; a modern compiler hypothesis is not historical evidence.

Implementation: `shared/rd-rend2/lightrecon/amplitude_fit.h/.cpp`, namespace
`Rend2LightRecon::Core::Amplitude`, version 1. `Fit(Request, ReadContext, Result)` is
the production entry point. The diagnostic result is not a canonical resolved-light
asset. It never supplies source-existence confidence or converts q3map Q to candela.
It reuses S01's `CpuForwardEvaluator`, factor cache keys, kernels and output encoder.
Python in this task only orchestrates tests, blind inputs and trusted evaluation.

## Observation operator and objective

For source j fit `Q_j=(Q_jr,Q_jg,Q_jb) >= 0`, bounded by an explicit box. Its
compiler gauge is `I_j=max(Q_j)` and `c_j=Q_j/I_j`, with zero handled separately.
There is no redundant free scalar intensity plus arbitrarily scaled colour.
Geometry, visibility, material/style policy and calibration are held fixed.

Let F_t(Q) be the selected production operator for observation type t. The
surface pipeline is:

```text
profile-specific direct + explicit profile ambient
  + independently supplied known pre-encoding terms
  + optional bounded/regularized surface baseline
  → profile encoding (including shared peak/gamma/exposure)
  → compare against encoded RGB8 intervals OR explicit relative-linear targets
```

Grid ambient and directed use the **unpacked single-pass grid reduction** in S01,
not the surface response. Grid split/dominant direction depends on the combined
RGB light mixture. Ambient is not relabelled GI, directed is not universally direct
irradiance, and global ordered grid-record compression is unsupported. Grid weights
default to zero; callers must opt in and attest the comparison scale/domain.
Deluxe directions are not RGB intensity targets in this stage.

For interval `[l_ic,u_ic]`, the signed residual is

```text
e_ic(p) = min(0,p-l_ic) + max(0,p-u_ic).
```

Missing channels are omitted. An exact relative target has l=u. For known
truncating RGB8, b<255 gives `[b,b+1]` (closed conservative relaxation); b=255 gives
`[255,+infinity)`. These are intervals **in output space**. The forward encoder
still jointly normalizes the complete RGB vector. A saturated red channel cannot
be treated as an independently clipped incident-radiance channel while ignoring
green/blue or shared-peak scaling. No gamma inversion of UnknownProfile is made.
`Rgb8()` is an optimization interval helper, not a new source decoder.

Surface weight uses actual covered world area a, reliability r and explicit
observation-type weight lambda_t:

```text
w_i = lambda_t a_i r_i / (3 sum_{k in type t} a_k r_k)
J(Q,b) = sum_i,c w_i rho(e_ic(F_t(Q,b))/sigma_i)
       + ridge/2 sum_j ||Q_j||²
       + sum_j soft_interval_prior_j(Q_j)
       + sum_j colour_weight_j/2 ||(Id - d_j d_j^T) Q_j||²
       + baseline_penalty/2 ||b||².
```

Here d is the unit-length direction of a known canonical Y=1 Rec.709 chromaticity
prior; its magnitude is not another amplitude parameter. Soft authored intensity
priors penalize distance outside a supplied RGB interval, permitting the data to
disagree. Hard boxes remain explicit caller constraints. The ridge and all prior
terms are reported separately. No penalty is inferred from a desired source count.

Losses are squared, Huber or Cauchy. Huber uses threshold delta in sigma-normalized
comparison units. Cauchy is nonconvex and is routed through bounded nonlinear
search even when the response itself is linear. Robustness to outliers is separate
from censoring: replacing 255 by an ordinary exact L2 target is not robust clipping.

For grid samples, `measure` is explicitly selected volume/sample weight, normalized
separately from surface area. `predictionScale` is an independently supplied fixed
mapping for relative/grid comparisons, never a fitted global gain. Sigma can express
declared calibration uncertainty. Unknown scale excludes the affected observation
and increments its own coverage counter. Encoded RGB requires predictionScale=1;
its transform belongs to the bake profile, not a post-encoding brightness control.

The optional baseline has one RGB vector for all surface samples, defaults to
disabled, requires positive regularization and explicit bounds, and never affects
grid. No per-luxel residual, unconstrained per-region ambient, indirect field or GI
point source can absorb all direct light. Known terms must be independently supplied
before encoding, with compatible units; their provenance remains the caller's duty.

## Linearity is a numerical capability, not fixed XYZ

`Bake::DescribeRecipe` exposes the already implemented profile recipe; it returns
Unavailable for UnknownProfile or unknown selectors. `Bake::ProveLinearResponse`
reuses the exact production `Value()` kernel and fixed geometry factor, with a
declared lower/upper Q box. Metadata `Require()` alone never authorizes a matrix.

| Feature/domain | Linear branch admissibility | Otherwise |
|---|---|---|
| Point/spot inverse square, FAST off | Fixed distance/normal/cone/visibility, independent RGB coefficient | Unsupported geometry/visibility is excluded/reported |
| Point/spot inverse square, FAST on | All possible peaks are inactive, or the minimum possible peak passes every envelope and pre/post-cone threshold; outward float-bound checks | Strict nonlinear operator |
| Linear attenuation | Only an identically inactive response is linear; active intercept/distance/cutoff depends on max(Q) | Strict nonlinear operator, no extrapolated A |
| Directional and pre-expanded convex polygon | Implemented fixed geometric response is linear before encoding | Unsupported tube/unknown expansion is unavailable |
| Surface relative255 | Proven source responses, compatible fixed known/baseline terms | Nonlinear profile source rules still require strict evaluation |
| Surface encoded RGB8 | Above proof **and** gamma=1, exposure=0, sRGB off, and maximum total pre-encoding peak <=255 over the full box; compensation is a fixed linear divisor | Full shared-peak/gamma/exposure encoding on each trial |
| Unpacked grid ambient/directed | Not admitted to the linear branch in v1; mixture direction/reduction couples sources | Separate strict grid operators |
| Unknown profile/transfer/primaries | No implicit formula or physical linear target | Unsupported/excluded with explicit reason |

Normal/two-sided and spot-angular branches depend on the fixed geometry, so no
XYZ/orientation derivative is claimed. Finite source expansion, lightstyles and
receiver placement must pass S01's feature-scoped capability check. Unsupported
receiver rows are counted, not made black. Unknown visibility is excluded and
reported; no complete comparable dataset returns success if all rows disappear.
Admission is determined independently of amplitude, so Q=0 cannot hide an unknown
blocker. Region membership is optional; no graph certificate is required to fit
otherwise valid world-space observations.

## Algorithms and stopping

**Linear:** matrix-free normalized projected coordinate minimization. Each
coordinate uses the current robust interval gradient and active curvature; trial
updates must not increase J. Curvature doubling and a full column-norm safeguard
handle active interval changes and robust weights. The objective is convex for
squared/Huber loss and nonnegative quadratic priors, subject to the proven branch
box. No mandatory source-by-luxel A is stored. Weighted column norms supply scaling.

KKT diagnostic is maximum projected gradient divided by weighted column norm,
using the actual box constraints and prior gradients. At a lower bound only a
negative derivative violates KKT; at an upper bound only a positive derivative
violates it. Default tolerance is 1e-7. Objective feasibility tolerance is 1e-10,
parameter motion 1e-4 Q and relative objective change 1e-10. Capped/stagnated results
remain `converged=false`; numerical stability does not imply identifiable sources.

**Nonlinear:** production predictions at every trial, bounded deterministic starts,
small local finite-difference damped least-squares proposals when <=24 parameters
and <=512 usable rows, accepted line search, then coordinate branch exploration.
Each coordinate considers zero/bounds/current plus fixed interior fractions and
golden-section refinement of the best sampled interval. This can cross dark FAST
plateaus; it is not a global optimizer or a proof that every disconnected active
branch was found. There is no fixed contribution matrix across support changes.

Default limits: 24 nonlinear passes, 22 refinement steps, 2 deterministic starts,
10,000 objective evaluations; 200 linear sweeps. Work limits and cancellation are
checked on streams, forward calls and arithmetic. The default candidate limit is
64 (configurable through 128), rows 1,000,000, batch 128, process-owned accounting
64 MiB, geometry-cache cap 8 MiB, 100 million source pairs, 2 million rays,
64 million traversal operations and 200 million arithmetic operations. These are
local fitting blocks; use S05 scheduling for larger candidate catalogs.

Every returned fit is re-evaluated without cached visibility through the strict
production evaluator. Linear predictions are compared numerically against that
strict result; nonlinear cached predictions are also compared. Default allowed
absolute difference is 1e-3 in comparison units (float canonical boundaries versus
double fitting arithmetic), never a tolerance for wrong-profile agreement.
Strict reevaluation uses the same admitted training rows and does not silently
drop newly unsupported rows. No validation/held-out target controls stopping.

`fitted=true` means a finite frozen diagnostic point was produced and strict replay
passed; it does not mean the source hypothesis is physically correct or accepted.
`converged`, stop reason, comparison coverage, maximum residual, objective,
conditioning and uncertainty must be inspected separately. Iteration exhaustion
can return a clearly non-converged diagnostic point. Work/cancellation/provider/
numerical errors return **no fitted source vector**, with available work/coverage
diagnostics. Nonlinear KKT is unavailable, not a zero-gradient claim.

## Rank, feasible amplitudes and source diagnostics

For small admitted parameter blocks the unregularized weighted linear response Gram
matrix is accumulated in streaming double precision and Jacobi-diagonalized.
Report singular values, relative rank threshold 1e-6, rank and condition when finite.
Ridge/chromaticity priors are deliberately excluded from this data-information
diagnostic. A nearly proportional pair can fit RGB accurately yet allocate Q badly.
For nonlinear blocks a local production-operator finite-difference response Jacobian
is reported, explicitly not a global rank/Fisher matrix or moving-shadow derivative.
Its sampled conditioning cannot rule out disconnected alternative amplitudes.

Linear coordinate slices intersect all comparison intervals while holding every
other parameter fixed. They are feasible ranges within the declared box, not joint
confidence intervals; noisy incompatible rows can make them unavailable. Saturation
and search-limit endpoints are explicit. Numerically proportional linear columns
add `AllocationExchange` records: `Q_a += step; Q_b -= ratio*step`, with bounded
step ranges. These are numerically near-null exchange proposals, not a certified
joint feasible set for every quantization interval or a calibrated confidence
region; priors can still prefer one allocation. Nonlinear conditional feasible intervals are currently unsupported,
not fabricated narrow error bars.

Each source returns Q, compiler peak/peak-normalized colour, zero-response and
allocation-ambiguity flags, conditional slices, retained prior and fixed-other
removal ablation when admissible. The ablation is an objective change, not source
existence confidence. Occluded/zero-response hypotheses stay in the result, with
zero Q for the ordinary zero-lower-bound case and unidentified amplitude status.
Physical absolute scale remains unknown. Canonical source calibration/mapping
records remain immutable in the Request; no new calibration system is introduced.

Data objective is also grouped by region and observation type. Global source
priors and removal ablations provide source-specific diagnostics; the coupled data
residual is not falsely partitioned into additive per-source RGB8 residuals.
Coverage has separate missing, colour-domain, disabled-type, calibration,
unsupported-geometry and unknown-visibility counters. Count is not independent
source information, and a rank diagnostic at fixed geometry does not estimate XYZ.

## Streams, cache and integration

`ChunkSource<Observation>` provides immutable, strongly owned bounded chunks with
strictly ordered stable IDs. The fitter retains a one-byte-per-observation admission
mask and small source vectors. Geometry factors are optional bounded blocks; no
RGB/held-out data enter them. `FactorDependencyKey` reuses S01's complete recipe,
source geometry, receiver placement, trace mode, material/observation generations
and geometry key. Amplitudes are excluded; FAST cutoffs are always recomputed.
Changing position/extent/visibility generation invalidates factors. Custom provider
leases must remain immutable and must change generation when their meaning changes.
No global cache, engine allocator, background task, OpenGL or `tr.world` dependency
is added. A cache is caller-owned and sequential; separate caches are needed for
concurrent fits. Cache hits consume no new rays; actual preparation/replay work is
reported. Count/byte/work limits fail explicitly; they do not truncate a fit.

S01 gains a recipe-validated encoder overload, geometry dependency accessor, linear
proof helper and preparation-work counters. Its lighting equations remain the same.
One numerical correction clamps the unpacked grid's dot(unit,unit) to [0,1], its
mathematical range, preventing tiny negative ambient values from roundoff. This
affects the CPU historical predictor only; runtime lightgrid sampling is untouched.

The X07 test adapter accepts a bounded `F` training frame and calls production
`Amplitude::Fit`. Ordinary X07 prediction frames and its existing Python optimizer
remain available. The new S06 worker receives only sanitized TrainingView,
supplied fixed candidate XYZ/count/type, public profile/settings and training RGB.
Trusted evaluation reads private amplitudes and validation/held-out RGB after the
C++ fitter has frozen parameters. Existing Python I/O guard denial tests are reused;
as before, that guard is not a sandbox against malicious native code. Hostile solvers
need an OS/container boundary with only allowed inputs mounted.

## Reproduction

Python tools use the standard library and existing project tooling; no SciPy/NumPy
runtime dependency is introduced. Generated test scenes are owned content; no
stock BSP/PK3 assets are committed or needed. The compiler-backed pipeline reuses
the pinned q3map2 BSPs, source/capture hashes, blind splits and positive-light gates
from X07, rather than rebuilding the capture/metric framework.

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build build/s06-gcc --parallel 2
ctest --test-dir build/s06-gcc --output-on-failure
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_amplitude_test lightrecon_bake_forward_tool --parallel 2
python -X utf8 tools/rend2/amplitude_fit/benchmark.py --tool build/s06-gcc/lightrecon_amplitude_test.exe --out tools/rend2/amplitude_fit/results/gcc.json
python -X utf8 tools/rend2/amplitude_fit/benchmark.py --tool build/lightrecon-core-renderers/Release/lightrecon_amplitude_test.exe --out tools/rend2/amplitude_fit/results/msvc.json
python -X utf8 tools/rend2/amplitude_fit/run.py --output build/s06-compiled-final-a --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe
python -X utf8 tools/rend2/amplitude_fit/run.py --output build/s06-compiled-final-b --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe
python -X utf8 tools/rend2/amplitude_fit/report.py --run build/s06-compiled-final-a --repeat build/s06-compiled-final-b --out tools/rend2/amplitude_fit/results
python -X utf8 -m unittest discover -s tools/rend2/amplitude_fit -p 'test_*.py' -v
```

Exact local Python may be invoked as
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.
Run output directories must be fresh. Default compiler inputs are the existing
`build/q3map2-bake-verified` and `build/x02/compiled-fixtures-final`; missing or
hash-mismatched fixtures fail clearly. No new q3map2 compile is claimed unless run.

Measured numerical results and final verification are recorded below after the
completed commands. Important limitations remain: historical stock transfer/bake
profile, full bounce, ordered compressed grid, moving/unknown optics, unsupported
curved receivers, missing source candidates and absolute photometric calibration.
Unknown direct-model discrepancy is not labelled recovered GI. High held-out
fidelity in a controlled fixed-XYZ scene is not general stock-map reconstruction.

## Final numerical witness (2026-10-29)

Final production sources pass **1,738 C++ assertions** under GCC 16.2 Release
(`-O3 -DNDEBUG`, fresh `build/s06-gcc`) and VS 2019/MSVC Release. Each executable
is run twice; numerical JSON witnesses match both within repeated runs and across
these two compilers. Canonical witness SHA-256 is
`d56dc899c08c0f4b3fd92b01da1e701457aa4ecaa20809016b48fe4ed9e9b406`.
Exact source/executable hashes are in `results/gcc.json` and `results/msvc.json`.

| Generated CPU fixture | Relative Q L2 error | Information/result |
|---|---:|---|
| Single point, relative-linear surface | 0 | Rank 3/3, projected KKT 8.65e-15 |
| Two separated coloured points | 1.75e-7 | Rank 6/6; maximum strict interval error 4.2e-5 |
| Same-colour overlapping sources + zero candidate | 2.06e-6 | Zero source retained; small float-target stagnation explicitly not converged |
| 20,000-Q bright source + 4-Q weak source | 6.01e-10 overall | Weak source error <0.3 Q; no brightness-based candidate removal |
| Coincident sources | 0.340 per-source allocation error | Zero RGB residual, rank 3/6; three joint exchange directions, ambiguous allocation |
| Nearly proportional columns | 0.251 allocation error | Tiny RGB residual; large condition and ambiguous allocation, not false exact recovery |
| One fully occluded candidate | Unidentifiable for that candidate | Its Q becomes zero, hypothesis retained/flagged; visible source recovered |
| Corrupted observations, Huber | 0.00346 | More than 5× improvement over L2 in the declared outlier fixture; corrupt residual remains visible |
| Tiny-area corrupted texel | 0 | Maximum bad-texel residual 1,000 remains reported; area weighting prevents it dominating |
| Soft authored intensity + chromaticity prior | 4.05e-7 | Data can disagree with the deliberately incorrect weak intensity prior |
| Linear encoded quantization / proven positive FAST box | 0.00146 | Feasible linear slices contain known amplitudes; full-box linearity proof passes |
| FAST encoded point | 0.00127 | Nonlinear branch; zero encoded interval error |
| Linear falloff | 0.000815 | Nonlinear branch; zero encoded interval error |
| FAST spot | 0.00272 | Nonlinear branch; zero encoded interval error |
| Shared-peak saturated source | 0.430 | Zero encoded residual does not identify total intensity; ambiguity retained |
| Censored relative-linear samples | 0.0917 | Feasible lower-bound solution, not exact uncensored amplitude recovery |
| Combined surface + unpacked grid components | 1.53e-8 | Nonlinear grid operator, separately normalized weights/calibration |
| Black observations | 0 (true Q=0) | No positive-source claim |
| Missing blue channels | Blue unidentifiable | Rank 2/3, ambiguity retained |

The ambiguous/occluded rows are **successful tests of honest non-identifiability**,
not successful amplitude recovery. Three small generated linear controls stop at
float-target/near-collinear stagnation with `converged=false` despite small residuals;
their status is deliberately not converted to convergence. Maximum observed
linear-versus-strict production difference is about 6.52e-5 in the large-Q control,
below the predeclared 1e-3 comparison tolerance. Nonlinear cached/strict differences
are zero on these controls. Failure controls cover UnknownProfile, unknown visibility,
bad/nonfinite intervals, measure-sum overflow, truncated providers, cancellation
mid-stream, memory/evaluation budgets and invalid unregularized nuisance baseline.

### Compiler-backed blind tests

All three pinned BSPs use the original X07 interior floor selection and independent
spatial partitions: 18 training, 15 validation and 15 held-out observations each.
These are compact supported subsets, not full-map coverage. Candidate XYZ/count/type
are **provided priors**. Only amplitudes are fitted; answers stay with the trusted
controller, which confirms source/BSP/capture/recipe hashes and original bytes.
Each case is fitted twice; two complete suites are also compared after removing
only named resource/timing fields. Private answer/validation/held-out read attempts
are denied in the worker. Known fixture answers appear below only in the trusted
evaluation report, not the input mount.

| Fixture | Fitted compiler Q | Relative source error | Train / validation / held-out interval RMSE |
|---|---|---:|---|
| `point_room` | (299.71445, 299.71445, 299.71445), truth (300,300,300) | 0.0952% | 0 / 0 / 0 |
| `two_colour`, source 1 | (299.49152, 35.95225, 8.01693), truth ≈(300,36,9) | 0.3665% | Joint: 1.65e-5 / 0.04086 / 0 |
| `two_colour`, source 2 | (12.77798, 60.07695, 301.02170), truth ≈(12,60,300) | 0.4202% | Same joint prediction |
| `door_occlusion` | (298.81796, 298.81796, 298.81796), truth (300,300,300) | 0.3940% | 0 / 0.01698 / 0 |

These small amplitude deviations are compatible with quantization/finite sample
constraints; zero interval error does not demand byte-inverted exact Q. Two-colour
chromaticity angle errors are 0.184° and 0.140°. Absolute cd/lux/nits error is
NotComparable. All three correct-profile fits use the nonlinear branch because
the full box includes FAST thresholds/coupled output regimes. No training-only
matrix extrapolation is substituted for that operator.

| Held-out control | `point_room` | `two_colour` | `door_occlusion` |
|---|---:|---:|---:|
| Correct supplied geometry/profile | 0 | 0 | 0 |
| Black predictor | 50.5054 | 35.1707 | 50.5054 |
| Regularized uniform ambient-only fit | 18.9136 | 12.8251 | 45.5317 |
| Wrong linear-falloff profile | 9.0376 | 9.7614 | 9.0376 |
| Wrong supplied XYZ | 25.3871 | 15.0420 | 22.9723 |
| Missing second source | NotApplicable | 6.6754 | NotApplicable |

All positive-light gates use the frozen X07 rules, unchanged. The oracle using
private true amplitudes independently gives held-out interval RMSE 0. Validation
and held-out colours never choose solver steps or stopping. `two_colour` wrong-XYZ
control remains `converged=false`; correct two-colour terminates at a bounded
parameter-step condition, not a claimed global nonlinear optimum. Sources are not
discovered or promoted by any of these results.

Original BSP SHA-256s:

```text
point_room      890a15b75b0ae24ddad2725ba24327f7e1949bf17d0e7fd6baef100a59feb2af
two_colour      0969f5a631e5a9bac95224f1831df1587bad62f883761c89b92231119eef6ec1
door_occlusion  5763cba0befb01a84686664338c7ce8cc8a1feb333e4a90bb9a74343ab5f9c39
```

### Resources and exact checks executed

| Production fit | Objective evaluations | Forward block calls | Rays including strict final replay | Time, MSVC measured run | C++ peak RSS |
|---|---:|---:|---:|---:|---:|
| `point_room` | 148 | 156 | 36 | 0.080 s | 4.20 MiB |
| `two_colour` | 9,390 | 9,404 | 72 | 5.34 s | 4.42 MiB |
| `door_occlusion` | 148 | 156 | 36 | 0.084 s | 4.33 MiB |

Preparation occurs once per small fixture; subsequent geometry factors are reused.
The two-colour fit is expensive relative to its six parameters because the bounded
nonlinear fallback explores quantized plateaus; this is an explicit optimization
limitation, not evidence of a fast full-map solver. Work spent on wrong hypotheses
is retained in `compiled.json`/`resources.json`. Python worker peak RSS is separately
about 16.7–17.0 MiB; it is not counted as C++ memory. Times are local concurrent-load
measurements and are not a compiler speed ranking.

The generated streaming control fits **25,000 observations**, maximum read **64**,
cache disabled, accounted owned/scratch bound **2,360,296 bytes**, 400,000 rays
across repeated matrix-free passes including strict replay/diagnostics. There is
no dense 25,000×sources matrix. Actual peak process working set for the complete
generated test executable is 4.40 MiB (GCC, two runs) and 4.88–5.04 MiB (MSVC,
two runs); full test-process times are 7.98–8.90 s and 12.31–14.32 s respectively.
Accounted allocation budgets and OS working-set measurements are different metrics.
These include all generated fixtures and the stream, not just one scalar solve.

Final GCC CTest: **22/22**, 19.97 s. Final MSVC S06 CTest: **1/1**, 11.05 s.
Both SP and MP Release renderer targets compile/link after the final implementation.
Build warnings about the existing x64 `/arch:SSE2`, missing Git tag and repeated
CPack setup do not indicate a numerical pass/failure. `git diff --check` succeeds.

Additional executed commands (local runtime path can replace `python`):

```powershell
python -X utf8 tools/rend2/amplitude_fit/run.py --output build/s06-complete-a --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe
python -X utf8 tools/rend2/amplitude_fit/run.py --output build/s06-complete-b --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe
python -X utf8 tools/rend2/amplitude_fit/report.py --run build/s06-complete-a --repeat build/s06-complete-b --out tools/rend2/amplitude_fit/results
ctest --test-dir build/lightrecon-core-renderers -C Release -R '^lightrecon_amplitude$' --output-on-failure
python -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/s06-forward-regression
python -X utf8 -m unittest discover -s tools/rend2/amplitude_fit -p 'test_*.py' -v
python -X utf8 -m unittest discover -s tools/rend2/bake_model -p 'test_cpp_forward.py' -v
python -X utf8 -m unittest discover -s tools/rend2/fixed_geometry -p 'test_*.py' -v
```

Python checks: S06 **4/4**, production forward interface **6/6**, existing X07
**22/22**, with no skips in these executed runs. The existing 22 compiled bake-model
comparisons report **20 supported cases passed** (19 byte-exact, colour clipping
within the predeclared one-byte bound) and **2 explicit UnsupportedBounceReplay**
controls. Bounce replay is not passed or implemented. The scoped 1,225-sample
multi-sun boundary replay remains exact. These are reads of pinned existing bakes,
not new compiler executions. No game or GPU visual validation was launched, since
no runtime lighting path changed.

Files added: amplitude core/API, standalone amplitude test, S06 worker/controller,
benchmark/report/protocol tests, README and versioned result witnesses. Files
extended: shared-core/standalone CMake lists, S01 forward helpers/work accounting,
X07 test server and an optional worker-script parameter in its controller. No
general-purpose numerical dependency was introduced. Before larger production
fitting, prioritize better bounded nonlinear active-set refinement, safe screening,
global/profile alternative handling, richer nonlinear feasible sets and explicit
physically justified calibration/transport. Those are not inferred from a low
RGB residual in this fixed-geometry stage.
