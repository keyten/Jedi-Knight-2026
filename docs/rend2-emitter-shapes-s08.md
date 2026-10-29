# S08: offline spot, rectangle and finite-cylinder hypotheses

`lightrecon/emitter_shape.h/.cpp` adds a bounded production CPU evaluator/fitter
for **fixed-count alternative emitter models**. It does not publish recovered
lights, set canonical `Resolved` state, alter LTC, fog, Forward+, PBR, shaders,
or select a historical compiler for stock JA. No game/GL process is required.

The implementation uses the canonical `PhysicalEmitter` geometry/frame and
photometry vocabulary, S01's production bake kernels and linear-response proof,
S06's interval/censoring semantics, and S07's BSP/visual swept-placement provider.
The RGB solve here is root-coupled: dozens of expanded facets share **one** source
RGB, rather than being fitted as independent lamps. It does not reuse a fixed
response matrix across changed geometry or nonlinear active branches.

## Explicit support matrix

| Source | Historical observation hypothesis | Separate diffuse-integral hypothesis |
|---|---|---|
| Point | S01 NetRadiant inverse-square/linear, normal/FAST/encoding recipe | Isotropic on-axis intensity / distance² |
| Spot | S01 `world-cone-16-32`; target distance/radius; no invented smooth inner cone | Specified smoothstep inner/outer cone, on-axis RGB intensity |
| Rectangle | Explicit convex polygon subdivision + S01 signed edge integral and per-polygon centre trace | Uniform Lambertian emitted radiance integrated over rectangular area |
| Two-sided rectangle | Explicit S01 `angular.twoSided` convention and its historical sign/near-plane rules | Absolute emitter cosine; same emitted radiance on each face |
| Open finite tube | **Assumed faceted polygon expansion**, opt-in, with declared sides/axial segments | Finite cylindrical curved side, uniform outward Lambertian radiance; no end caps |

There is **no verified native historical tube kernel**, automatic backsplash,
arbitrary shader-mask evaluation, arbitrary historical two-sided compilation
recipe, stock Raven angular law, compressed-grid fitter or GI residual here.
`allowFacetExpansion` and a provenance string are required for historical finite
surface hypotheses. `UnknownProfile` cannot be numerically evaluated without an
explicit supported alternative. Existing S01 evidence remains feature scoped;
wrapper tests do not expand its historical verification scope.

Historical source tessellation is part of the chosen **model**, not merely a
numerical integration resolution: each polygon has its own centre visibility ray
and historical near-plane handling. Increasing its subdivisions may alter the
predicted bake. Physical finite-source integration is separately labelled and
never silently substituted for compiler-faithful super1 sampling.

## Equations and source units

For a receiver `x`, receiver-to-source direction `u`, distance `r` in metres and
source propagation axis `f`, the separate physical model predicts incident
irradiance (no albedo division or outgoing-radiance assumption):

```text
point:      E_rgb = Q_rgb * max(0, n·u) * V / r²
smooth spot:E_rgb = point * smoothstep(0,1, clamp((-f·u-cosOuter)/(cosInner-cosOuter)))
area:       E_rgb = integral_source L_rgb * max(0,n·u) * max(0,-ns·u) * V/r² dAs
two-sided:  replace source cosine with abs(ns·u)
tube:       source(s,phi) = centre + right*s + radius*(up*cos(phi)+forward*sin(phi))
            s in [-halfLength,+halfLength], phi in [0,2pi), dAs=radius ds dphi
```

Receiver two-sidedness is independent of source two-sidedness. For tubes,
canonical `frame.right` is the longitudinal axis. Rectangles emit along
`frame.forward`; `cross(right,up)=forward`. Spot axes point along photon travel,
not toward the source. There is no arbitrary nonzero near-distance softening:
singular/too-near samples return `Unavailable` at the declared tolerance.

`Model::units.worldUnitsPerMeter` converts geometry to metres. For area sources,
area and squared distance conversions cancel; for point/spot intensity the
world-distance coefficient includes `worldUnitsPerMeter²`. Changing this scale
does not calibrate legacy brightness. `physicalToScene` is metadata only; the
fitter does not call runtime presentation/exposure transforms.

`Output::PhysicalLux` requires an explicit independently authored calibration.
Then point/spot `rgbAmplitude` is linear RGB on-axis intensity (cd basis), and
rectangle/tube amplitude is linear emitted radiance (nits basis). Returned
canonical emission uses Rec.709 luminance Y and Y=1 chromaticity. Absolute scale
is known only in this explicitly calibrated case. Relative integral hypotheses
use the same geometric law with **relative** intensity/radiance; their values do
not become measured candela/nits. Historical Q is always compiler-relative.
Canonical Y/chromaticity are populated only with explicit linear-Rec.709 primaries
attestation; otherwise fitted RGB remains in the diagnostic and canonical colour/
intensity stay unknown. Physical-lux output requires that attestation. Historical
spot output keeps canonical angular profile `Unknown`, with fitted compiler
radius/target parameters stored separately.

The historical spot law is inherited exactly from S01: with target distance `D`,
radius `R`, axial distance `a`, and transverse distance `t`, use
`clamp(((R+16)*a/D-t)/32,0,1)` with the recipe's point/spot attenuation and cutoff.
Fitted half-angle `atan((R+16)/D)` is a convenient equivalent parameter, **not a
recovered physical inner/outer smooth cone**. `D` remains the explicit fixed
recipe parameter; a separate freely scaled radius/distance gauge is not added.
See [bake model](rend2-bake-model.md) and [S01 evaluator](rend2-bake-forward-evaluator.md)
for source-locked equations/evidence and declared verification scope.

## Sampling, optics and accuracy

`Shape::Prepare` accepts source geometry, leased receiver geometry and immutable
visibility generations, **no target RGB**. A physical block uses deterministic
midpoint source quadrature: rectangular tensor grid or cylinder angle×length.
Receiver footprint triangles are subdivided in barycentric/world geometry and
integrated with covered-area weights and interpolated normals. With no footprint,
the operator explicitly evaluates a point receiver; it does not fabricate a
square luxel from sample size. Source/receiver subdivision has hard limits.

An optional doubled source/receiver rule supplies a nested discrepancy. The finer
result is returned; its difference from the coarser rule is **not a certified
total error bound**. Both grids can miss a very thin visibility event. High-cost
boundary/source-size claims require resolution studies or the stricter existing
physical footprint reference where applicable. No true boundary derivative or
universal shadow-width correction is advertised.

Every relevant source node has an actual CPU visibility ray. Known opaque hits
are zero contribution. Unknown alpha/translucent/unsupported blockers or assumed
visibility make the entire affected prediction unavailable, not transparent or
black. Only a named emitting source surface may be excluded at its endpoint
within a specified tolerance. The canonical source/receiver position is unchanged;
the explicit receiver ray offset is part of the observation operator and key.

Historical preparation/evaluation calls `Bake::CpuForwardEvaluator::Prepare` and
`Evaluate`, then encodes the **summed pre-encoding** result once. Shared-peak RGB
normalization/quantization is not applied independently to facets or subtracted
after clipping. Geometry factors remain independent of Q; FAST/cutoff activity is
recomputed at every amplitude evaluation through production kernels.

## Fitting parameters, constraints and priors

`Shape::Fit` receives a training-only immutable `ChunkSource<Observation>`, map /
observation identity, explicit model, candidates and certified placement inputs.
It has no validation/held-out source or ground-truth reader. The trusted caller
freezes all results before calling `EvaluateFrozen` on independent folds.
`Observation` must attest correct comparison domain and contains per-channel
intervals, area, sigma, spatial-block ID and role. Unknown colour transfer is not
made comparable by this module. Existing dataset `TrainingView` sanitization and
separate controlling-process access remain the deployment boundary; a C++ role
enum alone is not an OS security sandbox.

Parameterization:

* XYZ offsets have hard centre bounds and maximum distance from initialization.
* A local exponential-map rotation acts on the initial orthonormal frame. Spot
  and tube axes have two rotational degrees of freedom; rectangles have three.
  Source frames are regenerated and validated, not optimized as nine free values.
* Rectangle half-width/half-height and tube half-length/radius are log offsets,
  strictly positive and bounded.
* Physical cone outer angle and inner/outer angle ratio obey strict angle order;
  the historical law fits only its supported equivalent radius/cone parameter.
* One-/two-sided rectangle emission is a discrete alternative supplied by the
  controller, not a continuous transparency switch.
* RGB is nonnegative, finite, bounded and has no redundant scalar×free-RGB gauge.

Authored/visible fixture candidates require explicit nonzero geometry priors.
Position, rotation and log-size prior scales/weights/provenance are retained.
Hard motion/rotation/size limits remain active even for inferred candidates.
No confidence value is manufactured from the evidence tier. Strong authored
geometry should normally have narrow bounds plus appropriately scaled priors;
using a tiny positive prior is a caller policy, not automatic physical certainty.

The complete emitter is covered by a bounded union of support cubes: 8×8 for
rectangles, 32 axial pieces for tubes. Each cube's enclosing local radius covers
the full surface patch/cross-section, not just sample nodes. For motion/rotation,
inflate each by `2*Rmax*sin(relativeFrameAngle/2)`; this bounds deviation from the
straight segment during shortest-frame rotation and interpolated dimensions.
S07 certifies every initial/final local volume and swept segment with BSP boxes
and local visual geometry. This allows a long thin tube near a floor while still
rejecting wall crossings. The union is conservative and may reject tight recessed
fixtures; it never uses region/PVS membership as an optical certificate.

The objective uses world-area weights, normalized across admitted training rows:

```text
sum_i area_i/totalArea sum_c Huber(distance(predicted_ic, interval_ic)/sigma_i)
 + ridge/2 sum_sources ||Q||²
 + 1/2 sum_geometry_parameters priorWeight*(parameter/priorSigma)².
```

Clipped output channels are lower-bound intervals in the actual encoded domain,
with shared-peak coupling evaluated before that interval comparison. There is no
reverse clipping, arbitrary per-luxel residual, ambient/GI term or PBR-albedo fit.
No admitted row can be dropped when trial geometry produces unknown visibility:
the trial is rejected as unavailable, retaining fixed comparable support.

Every geometry trial refits RGB. Blocks prove amplitude linearity over the full
lower/upper Q box before bounded coordinate/NNLS-style updates. Physical fixed
sample geometry and simple pre-encoding branches are linear. Reachable nonlinear
encoding, FAST/cutoff changes and unsupported linear attenuation branches use
bounded knot/golden-section amplitude search through the production value.
No one-amplitude matrix is extrapolated through changed support. The method
shares S01's actual proof and S06's interval/Huber mathematics, but performs a
root-coupled solve here because expanded facets must share source amplitudes.
Projected KKT is explicitly available only for the proven linear amplitude block
at search resolution; a zero-initialized field on the nonlinear path is not a
measured zero gradient. It is not the full geometry or final-refinement KKT.

Geometry uses coarse-to-fine bounded polling, accepted-cost parabolic proposals
and strict generalized-pattern extrapolation. Each proposal has fresh placement,
visibility and RGB fitting. Broad-cone plateaus require explicit bounded training-
only angular starts before local refinement. Binary visibility is not assumed
globally differentiable. This is local optimization; sweep/trial/work exhaustion
and amplitude convergence are distinct from accurate parameter identification.
For at most 128 training rows and 24 joint parameters, a scaled/damped finite-
difference Gauss–Newton proposal addresses coupled XYZ/cone/amplitude valleys.
The numerical step is only a proposal: every accepted update is fully certified,
RGB-refitted and compared using actual strict values. Unknown perturbed visibility
disables the proposal. It does not advertise a correct shadow-boundary derivative
or a calibrated covariance, and larger datasets do not allocate that Jacobian.

## Alternative-model comparison

`EvaluateFrozen` scores validation or held-out rows using frozen parameters,
identical domain, weights, intervals, map/geometry generations and source IDs.
It reports comparable/requested/unknown counts, independent spatial blocks,
interval MSE, black-predictor MSE, maximum error, work and quadrature discrepancy.
Unknown predictions prevent full-coverage availability; missing is not zero error.

`Select` accepts **validation** scores only. Every alternative must have identical
observation/target/geometry identity and compatible output units. The declared
score is `validation interval MSE + complexityPerParameter * fittedParameterCount`.
The coefficient and comparison slack must be frozen by the benchmark/controller
before looking at held-out results; this is a transparent regularized criterion,
not BIC, a calibrated posterior or an arbitrary normalized confidence.
The minimum is reported as `preferred` separately from `retained` alternatives.
Different historical/physical laws are not compared merely because both outputs
are called relative RGB; selection requires the same law as well as compatible
units. A justified observation-domain/calibration bridge is future work.

Comparable alternatives within the declared slack/numerical-discretization margin
are retained. A flexible model cannot win solely from having more variables.
Black/no-positive/incomplete predictions do not meet the positive-signal gate.
With fewer than three independent spatial blocks, selection cannot claim a
discriminated type. Broad spots without independent directional evidence retain
no precise angular-classification claim even if they are the preferred predictor.
Selection describes **conditional model preference**, never verified source type.

Uniform emitted RGB is the only supported spectral model. A textured fixture's
average emission colour does not identify a spatial chromaticity distribution.
Two adjacent roots remain two separately fitted source IDs; facets do not become
new lamps. Tube radius×radiance, far-field area×radiance, broad spot×point and
single-plane XYZ/cone/gain ambiguities remain real. The module does not issue
falsely narrow shape/intensity intervals or source existence probabilities.

## Determinism, resources and integration

All work is synchronous and CPU-only. Root order is sorted by canonical ID.
Geometry generations, frames/dimensions, profile, subdivision recipe, receiver
geometry/footprints and trace settings key bounded factor blocks. Q is excluded
from geometry keys; source movement/rotation/extent changes invalidate them.
No global ray-pair cache or obligatory all-map source×luxel matrix exists.

Streams are read in bounded blocks. Source count, geometry parameters, expanded
polygons, numerical nodes, memory, trace work, amplitude values, sweeps and trials
are capped; cancellation returns no partial emitters. Errors preserve diagnostic
work, not fake zero lights. `FitResult` remains a candidate diagnostic containing
initial assumptions, canonical geometry, typed fitted emission, trace and strict
frozen reevaluation. Neither runtime assets nor a complete solver are enabled.

The shared CPU-core CMake list includes the module for SP and MP. Standalone
`lightrecon_shape_test` and `tools/rend2/emitter_shape/benchmark.py` exercise it
without a renderer. The Python script only repeats C++ runs, checks semantic
determinism and measures OS peak working set; no fitting is reimplemented there.

## Reproduction and validation scope

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/s06-gcc --parallel 2
python -X utf8 tools/rend2/emitter_shape/benchmark.py --tool build/s06-gcc/lightrecon_shape_test.exe --out tools/rend2/emitter_shape/results/gcc
ctest --test-dir build/s06-gcc --output-on-failure
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_shape_test --parallel 2
python -X utf8 -m unittest discover -s tools/rend2/emitter_shape -p 'test_*.py' -v
```

The existing configured MSVC renderer build is used; the standalone target has no
engine libraries. Local Python is
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.
No third-party numerical dependency is added.

Owned tests include all four source types, two-sided sheet, adjacent panels,
partial occlusion, broad/misclassified spots, missing shape evidence and calibrated
SI control. Targets use a denser quadrature than fitting for extended physical
sources. Independent controls include analytic point/spot on-axis response,
closed-form rectangle irradiance and cylinder far-field projected-area asymptote.
The three spatially offset folds have no duplicated coordinates or texture-filter
footprints; compact synthetic probes are not a claim of realistic stock coverage.
Additional adversarial checks cover optics, singular/invalid dimensions/frames,
historical tube opt-in, UnknownProfile, nonlinear FAST/shared-peak clipping,
geometry-key changes, corrupted streams, role mismatch, cancellation and budgets.

Machine-readable witnesses live in `tools/rend2/emitter_shape/results/` and exclude
timing/RSS from deterministic output. These are controlled tests, not stock
physical ground truth. The owned fixtures explicitly use **1 world unit/metre**
for simple analytic checks; production callers must retain actual dataset units
(the canonical JA default is approximately 33.3333 WU/m).

## Executed results and counterexamples

Final GCC and MSVC builds each passed **2,910 assertions twice**, with identical
numerical witnesses within each compiler. Source/executable hashes are frozen
around both runs. No bitwise agreement across different compiler optimizations
is assumed. The independent point and rectangle formulas and cylinder far-field
check passed. Historical point/spot/polygon wrapper predictions equal direct S01
CPU predictions at all tested receivers within `1e-7` RGB vector difference.

| Fixture | Parameter result | Held-out interval MSE | Black MSE |
|---|---|---:|---:|
| True point | XYZ error 13.856 → 0.00002294 WU | 1.03e-14 | 0.86515 |
| Narrow spot, receivers at multiple heights | XYZ 5.6569 → **0.013981 WU**; axis error below float angular resolution | 1.06e-8 | 4.8428 |
| Rectangle | half-size 32×16 → 32.870×16.461 WU | 1.42e-6 | 0.65005 |
| Long tube, distant receivers | length/radius/radiance **not uniquely recovered**, see below | 5.56e-6 | 0.16665 |
| Two-sided sheet | Positive observations on both sides | 5.45e-7 | 0.65005 |
| Two adjacent panels | Two stable root IDs and independent RGB retained | 3.38e-6 | 2.5065 |
| Partially occluded rectangle | Opaque geometry retained; finite sampling error remains | 4.88e-5 | 0.28321 |
| Near-field tube | length **96.0803 vs 96**, radius **4.89068 vs 5** WU | 1.57e-5 | 6.2535 |
| Broad spot | Perfect RGB without cone identification; precise angular flag **false** | 0 | 0.86515 |
| Panel pose | XYZ 4.2426 → 0.23195 WU; frame error 0.00045853 rad | 1.11e-6 | 0.6448 |
| Tube axis | axis error 0.0058083 rad (≈0.333°) | 1.29e-6 | 0.16664 |
| Historical spot | Relative Q (300,120,40) recovered; angular profile remains historical/unknown canonically | 7.94e-13 | 673.01 |
| Authored SI panel | Nits calibration retained; about 0.106% radiance-amplitude bias from finite quadrature | 5.45e-7 | 0.65005 |

Noiseless synthetic precision is not stock-map localization accuracy. In particular,
the distant tube fit gives half-length **56.4294 vs 48**, radius **11.4756 vs 5**,
and red radiance amplitude **7.3303 vs 20** despite its tiny RGB error. This is a
counterexample to identifying tube geometry from low residual alone. Near-field
receivers and denser declared source integration substantially reduce the tradeoff:
half-length 48.0402, radius 4.89068, red amplitude 20.4184. These are point estimates,
not calibrated joint intervals; stronger independent fixture geometry is still
needed before physical certification. Rectangle width/height and emitted radiance
also show coupled discretization/extent bias rather than exact physical recovery.

### Same-fold type comparison

Every row below fits all alternatives to the same training data, freezes validation
preference, then evaluates the **same held-out observations at strict sampling**.
Candidate centres/count are supplied priors, not discovered. Parameter penalties
are `1e-4` per fitted parameter for the point-overcomplex control, `1e-7` for the
panel/tube/spot controls and zero for the deliberately underconstrained case;
score slack is `1e-5`, except `1e-4` in that underconstrained case. These numerical
fixture criteria are not universal stock-map classification thresholds.

| Hidden type / control | Point held-out MSE | Spot | Rectangle | Tube | Preferred / retained |
|---|---:|---:|---:|---:|---|
| Point | 0 | 0 | 7.16e-4 | 4.77e-4 | point / point,tube |
| Rectangle | 2.147e-2 | 2.147e-2 | 7.67e-5 | 2.592e-2 | rectangle / rectangle |
| Tube | 2.175e-4 | 2.175e-4 | 1.185e-4 | 1.44e-5 | tube / rectangle,tube |
| Narrow spot | 0.15876 | 0.001943 | 0.13431 | 0.10286 | spot / spot,tube |
| Tiny area, only one receiver per fold | 1.43e-4 | 1.43e-4 | 6.92e-5 | 5.86e-4 | rectangle / rectangle,tube; insufficient independent blocks |

`preferred` is the minimum **penalized validation** score; held-out never chooses
parameters. Retained alternatives are broader than low-error alternatives when
source discretization is unresolved. For example, the wrong tube explanation of
the spot has a very large coarse/fine discrepancy (about 0.746 normalized channel
units), so it remains numerically unresolved even though its prediction is poor.
This is **not positive physical evidence for that tube**. Source quadrature must
be refined and independent fixture/shadow evidence added before excluding or
using such a model. The discrepancy margin is diagnostic, not a guaranteed error
interval or a posterior. Correct preference in four controlled scenes is not
general source-type recognition and does not justify confidence 1.

### Work, memory and regression checks

The complete S08 executable including reference, negative, fitting and four-model
comparison controls took **3.59 / 3.63 s** in the recorded GCC runs and **4.35 /
4.74 s** in MSVC. GCC OS peak RSS was **5,840,896 / 5,902,336 bytes**; MSVC was
**5,599,232 / 5,922,816 bytes**. These are process working-set counters, separate
from a 64-MiB configured fit limit and conservative cache/block scratch preflight.
Measurements reflect concurrent local workload, not a cross-compiler speed claim.

Representative GCC fit work (strict final evaluation included): point 72 trials /
3,741 rays; multi-height spot 327 / 11,438; panel pose 304 / 265,269; distant tube
405 / 372,180; near-field tube 68 / 937,280. Diagnostic performance must count
source-node rays, not just optimizer iterations. Near-field fidelity costs much
more than a point response; memory remains bounded by tiled blocks.

* GCC standalone tests excluding S07: **23/23**, 20.84 s, including S08.
* S07 independently: **1/1**, 207.63 s. Across those invocations all **24** CPU
  tests passed. S07's numerical implementation was not changed by S08.
* MSVC Release S08 CTest: **1/1**, 4.11 s; repeated witness has 2,910 checks/run.
* SP and MP Release DLLs compile/link with the final shared-core implementation.
* Python witness-gate tests: **3/3**. Black, missing, nonfinite and forced-unique
  outputs cannot pass those gates.
* Existing pinned compiled-BSP forward comparisons: **20 supported cases passed**,
  19 byte exact and one-byte colour clipping. `bounce1`/`bouncegrid1` remain explicit
  `UnsupportedBounceReplay`. This reuses existing bakes; no new q3map2 build or
  historical native tube bake is claimed.
* Worktree shader-input/binding comparison to the pre-S08 HEAD passes **27 programs
  / 51 stages**. No new GL compilation or game/visual test was performed.
* `git diff --check` passes. Existing x64 `/arch:SSE2` warnings are unrelated.

Additional commands actually executed:

```powershell
python -X utf8 tools/rend2/emitter_shape/benchmark.py --tool build/lightrecon-core-renderers/Release/lightrecon_shape_test.exe --out tools/rend2/emitter_shape/results/msvc
ctest --test-dir build/s06-gcc -E '^lightrecon_point_refine$' --output-on-failure
ctest --test-dir build/s06-gcc -R '^lightrecon_point_refine$' --output-on-failure
ctest --test-dir build/lightrecon-core-renderers -C Release -R '^lightrecon_shape$' --output-on-failure
python -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/s08-forward-regression
python -X utf8 tools/rend2/point_refine/compare_shader_inputs.py --reference HEAD --output tools/rend2/emitter_shape/results/shader-delta.json
git diff --check
```

The shared shader witness pins the actual reference commit/hash. Concurrent
unrelated renderer commits are not treated as S08 modifications. No proprietary
BSP/lightmap assets are copied into the new fixture/results directory.

Early development runs exposed actual failures: zero-push rays originating on a
receiver plane were unknown; selected boundary receivers lay in the synthetic
wall; cone plateaus defeated local polls; and joint XYZ/cone/gain fitting could
reduce RGB error while worsening XYZ. The final declared compiler recipe uses
its actual one-unit receiver push; physical sampling uses a separate 0.001-unit
ray offset. Boundary receivers were corrected in fixture geometry; the optimizer
gained explicit angular starts and strict joint proposals. Test tolerances were
not relaxed to hide those failures. A transient Windows all-target link failed
because S07's test executable was still running; after it completed, the rebuild
and CPU suite passed. No baseline or original compiler target was rewritten.

## Remaining limits before wider reconstruction

* No verified stock Raven source/bake profile or absolute stock brightness.
* No native historical cylindrical emission; faceted expansion is an assumed,
  versioned recipe, with sampling-dependent bake shadows.
* No inferred finite spot disk/extent, arbitrary angular tables or spatially
  varying emitted colour. Unknown extent/profile remains explicit.
* Finite-source quadrature can miss sub-grid optical events. Nested discrepancy
  is not a certified shadow/energy error bound; final high-quality boundary work
  needs adaptive/reference integration beyond this bounded implementation.
* Root geometry/count are priors. No source discovery, global count optimization,
  GI attribution, or calibrated type/existence/parameter probability is provided.
* Region/material/visibility uncertainty is not repaired by fitting. Unsupported
  optics and corrupt inputs remain unavailable; no extra lamp or free residual
  field hides them.
