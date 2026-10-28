# S01: profile-scoped CPU baked-light observation evaluator

Implemented 2026-10-28 against `rend2-unified-wip`, base commit
`fe16bce7729fd4c77b62d55abb4718da217882f6`. This is forward prediction for
specified sources, **not inverse reconstruction or source verification**.
An empty prediction, a valid asset, successful profile preflight and low RGB
error do not establish that physical lights have been recovered.

## Implementation and dependencies

`shared/rd-rend2/lightrecon/` contains:

| File | Responsibility |
|---|---|
| `bake_profile.h` | Version 2 profile/request/result contracts; feature evidence, units, modes, statuses, work and uncertainty flags |
| `bake_forward.h/.cpp` | Concrete `Bake::CpuForwardEvaluator`, capability registry, known direct kernels, encoding, optional factors and geometry-only luxel adapter |
| `bake_trace.h/.cpp` | Restricted source-locked compiler trace-tree replay, with explicit complete opaque/sky coverage assertion |
| `bake_integral.cpp` | Separate SI planar footprint integral and finite-rectangle reference |
| `bake_math.h` | Internal double-precision arithmetic, not a new public geometry convention |

All compile once in `rend2_lightrecon_core`, already shared by SP, MP and
standalone tests. There is no GL, `tr_local.h`, world pointer, engine allocator,
filesystem operation, cvar, fit optimizer or publication call in this module.
The existing renderer and observation capture paths are unchanged. No renderer
consumer or recovered/exact-light capability is enabled.

```text
immutable mapped geometry + specified source hypotheses + explicit profile
    -> metadata preflight -> numerical capability check
    -> geometry factors -> profile-specific source values -> visibility
    -> separate direct/sun/sky/explicit ambient accumulation
    -> requested observation operator -> shared encoding -> quantization

authored SI photometry + explicit covered footprint + bounded local geometry
    -> separate physical integral (lux), never substituted for compiler super1
```

Only C++11 and the standard library are required. Source-derived GPL-compatible
formulas are attributed to the inspected NetRadiant source; no third-party
numerical or automatic-differentiation package was introduced.

## Profiles, evidence and admission

`NetRadiantProfile(Recipe)` constructs `NetRadiant-d9338309e-subsets-v2` variants.
IDs include a recipe digest. Full content keys include feature selectors,
parameters, evidence, applicability and source/catalogue identities. The profile
records the full source revision and pinned executable SHA-256:

```text
d9338309ea8fab75027b7382efaabe034302b862
894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4
```

`sourceLockKey` and `catalogueKey` pin the existing `source-lock.json` and
`profiles.json` using LF-normalized file bytes, independent of Git CRLF checkout
settings. Those historical research files retain their own v1 schemas;
the new implemented C++ contract is v2. It does not reinterpret old profiles or
silently reuse their cache identity. The canonical lighting-asset disk format
does not change. Future fitted assets must retain the **full** profile key,
geometry/material generations, observation policy and source expansion recipe.

Each feature has `Unknown`, `Assumed`, `SourceSupported` or `VerifiedSubset`
evidence, a selector/formula version, provenance and verification scope.
Attenuation, angle, point scale, spot, polygon, sun/sky, explicit surface ambient,
unpacked grid, deluxe, receiver placement, filtering and output encoding are
separate features. Bounce, absolute bake calibration, flood, dirt, styles other
than zero, global grid packing and general filtering remain unsupported.

`Require()` checks metadata only. `Capability()` additionally checks implemented
selectors, source/receiver type, output, mode, style and requested operations.
Inventing a `VerifiedSubset` metadata entry for bounce does not implement bounce.
`Recipe` does not infer compiler settings from target colours. Ambient defaults
to zero. Sky17 requires explicit J=3; unsupported expansions are rejected.

Dataset applicability defaults to `Unassessed`. Evaluation requires explicit
`allowAssumed` or an independently established `VerifiedFeatureSubset`
applicability. On stock JA, the caller must supply an actual candidate profile;
`UnknownProfile` itself has no numerical law. It produces `UnspecifiedModel`,
not guessed attenuation, arbitrary gain, sun direction, source-linear RGB or SI.
Evaluate multiple explicit candidates in separate calls and retain disagreement;
these conditional predictions do not identify Raven's production compiler.

## Three modes and the support matrix

`CompilerFaithful` means the selected compiler recipe is the prediction target;
it does not declare every trace/receiver combination faithful. Ordinary results
remain `ConditionalGeometry`. Only the content- and operation-scoped witness
below receives `ScopedCompilerReplay`. `PhysicalIntegral` is a separate SI
quantity. `OptimizationSurrogate` currently uses centre sampling/geometry traces,
has `Surrogate` fidelity and an unknown approximation error; it supplies no
claimed moving-boundary derivative. It must not be accepted as a strict final
value merely by relabelling its output.

| Profile / source | Output / sampling | Implemented scope and numerical evidence |
|---|---|---|
| NetRadiant inverse-square point | Surface relative RGB, prequantization encoded RGB, RGB8; planar super1 | `P*A/max(16,sqrt(r²+extra²))²`, FAST envelope; controlled point, scale, angle, extra-distance, colour cases |
| NetRadiant linear point | Same | `max(0,A*P/8000-d*fade)` with amplitude-dependent range; linear/no-angle/fade/Wolf controls |
| NetRadiant spot | Same | Forced inverse-square/cosine source, target-distance/radius cone; on-axis and angular-edge fixtures, independent oblique spot |
| NetRadiant expanded polygon | Same | Signed edge form factor, source-plane rules, explicit `.75` surface parameter scale; verified unoccluded ceiling panel only |
| Native tube | Any historical output | Unsupported. Caller may provide separately justified expanded polygons; no hidden RECT approximation or source expansion |
| NetRadiant directional sun | Surface RGB | Explicit incoming `-forward`, sky-reaching trace, no inverse-square falloff; sun and scoped multi-sun controls |
| NetRadiant sky17 | Surface RGB | Explicit 17-direction J=3 expansion and per-direction sky queries; positive aperture and sealed-sky controls |
| NetRadiant explicit ambient | Surface RGB | Explicit surface term before encoding; zero default. Never inferred as bounce or copied into grid ambient |
| Point/spot | Unpacked grid ambient, directed, dominant direction | Single-pass contribution reduction; no surface push/cosine. Distinct from surface RGB; limited comparison with lossy BSP records |
| Point/spot/ambient | Deluxe world direction and byte payload | Brightness-weighted incoming direction plus documented normal bias; controlled single-source/ambient cases |
| Sun/sky/polygon deluxe; native vertex light | Direction/RGB | Unsupported numerical combinations |
| Any profile | Full bounce, ordered grid compression, animated styles, arbitrary supersampling/filtering, unknown optics | Unsupported, not fabricated zero lighting |
| Authored point/spot/directional | SI footprint irradiance | Exact planar shadow clipping plus adaptive smooth-response quadrature; point checked against independent solid angle and moving-edge formulas |
| Authored rectangle | SI footprint irradiance | 8×8 source midpoint quadrature plus planar receiver clipping; finite-source total error remains unknown, not a certified bound |
| UnknownProfile | Any historical numerical output | Unspecified model. Explicit assumed NetRadiant candidates are separate hypotheses |
| Any historical output | Source-linear physical RGB / candela/lux/nits | Unsupported conversion; relative compiler channels are not calibrated measurements |

All general trace combinations remain conditional even when their controlled
fixture passes. A supported combination does not imply support for its Cartesian
product with every other verified feature. Scalar/geometry derivatives are not
part of the implemented public interface.

## Equations, units and stage ordering

For legacy source RGB amplitude `Q`, define `I=max(Q)` and `c=Q/I` (zero source
stays zero). `P=I*photonsPerParameter`, normally 7,500. Distances are in **world
units**. The raw direction uses raw distance; the attenuation denominator uses
`d=max(16,sqrt(r²+extraDistance²))`.

For surface point lighting, `A` is the selected cosine/no-angle/half-Lambert
response, with explicit angle scale. The receiver plane rejects back-facing
one-sided sources even in no-angle mode. Two-sided receivers use the other
normal side. FAST rejects sources outside their amplitude-dependent envelope
and discards scalar contributions at/below tolerance before colour multiplication.
Spot cone attenuation is applied in its documented order and rechecked against
FAST tolerance. A legacy spot is **not** the canonical physical smooth cone:

```text
axial = dot(receiver - source, propagationAxis)
coneRadius = (authoredRadius + 16) / targetDistance * axial
cone = clamp((coneRadius - radialDistance) / 32, 0, 1)
```

Polygon source integration uses the locked `PointToPolygonFormFactor` edge
integral and plane-near handling. It does not silently create backsplash lights,
subdivide emissive surfaces or integrate arbitrary partial source visibility.
The input winding must already be expanded, finite, planar, convex and
non-degenerate. Authored source geometry is not modified.

Sources accumulate in ascending stable ID order using doubles. Local direct,
directional sun, sky and explicit ambient are kept separate **before** encoding.
For the selected `ColorToBytes` rule:

```text
v[c] = 255 * (max(0, total[c]) / 255)^(1/gamma)
peak = max(v)
gain = min(1, 255/peak)                    when exposure == 0
gain = 255 * (1 - exp(-peak/exposure))/peak otherwise
encoded[c] = v[c] * gain / compensate
RGB8 = truncate(encoded)                  in the ordinary non-sRGB branch
```

The optional sRGB branch performs its own rounded byte conversion. Out-of-range
C byte-cast behaviour is rejected rather than emulated by integer wrapping.
`compensate<1` is deliberately unsupported. Shared peak normalization couples
channels: `[510,255,0] -> [255,127.5,0] -> [255,127,0]`. Quantized source components
cannot be summed or subtracted to obtain physically meaningful contributions.
RGB8 255 is an encoded endpoint/censored observation, not exact irradiance.
The frozen evaluation harness supplies the appropriate quantization intervals.

Grid reduction separately normalizes `sum(length(RGB_j)*incoming_j)`, projects
each RGB on the dominant direction for directed RGB, and accumulates
`.25*(1-projection)*RGB` into ambient. These are **compiler categories**, not
definitions of physical direct and indirect light. Final minlight/nonzero bytes,
grid nudge, style overflow, ordered first-match merging and bounce persistence
are not reproduced. Comparisons to packed BSP grid records explicitly retain the
existing merge-4 plus quantization-1 discrepancy budget.

Deluxe accumulation is another operator, with its own brightness weights,
ambient-normal bias, normalization and signed-component byte encoding. It is
not a normal map or a lightgrid direction. Its supported payload does not certify
that a missing deluxe observation exists.

The physical reference instead evaluates

```text
E_A = (1/area(A)) * integral_A integral_source V(x,y) * response(x,y) dsource dA
point: response = RGB_candela * max(0,n·incoming) / distance_metres²
area:  response = RGB_nits * cos_receiver * cos_emitter / distance_metres² * darea_metres²
sun:   response = RGB_lux_perpendicular * max(0,n·incoming)
```

It requires physically authored calibration, matching photometric units and
canonical linear Rec.709 D65 colour with Y=1. `worldUnitsPerMeter` converts
geometry; `physicalToScene` is presentation and is not used in this lux result.
The angular law must also be explicit: isotropic point, smooth-cone spot,
Lambertian rectangle or parallel directional source. Unknown emission laws are
unsupported rather than silently replaced with these models.
No default physical clamp at 16 world units is introduced. Finite angular sun,
sphere/tube reference geometry and curved/variable-normal footprint integration
are unsupported. General rectangle source quadrature is approximate.

A canonical physical source may enter historical evaluation only with an
explicit `compilerParametersPerCanonicalUnit`, matching calibration ID,
calibration reference and method. The resulting compiler output remains
relative. No code equates `light=300` with 300 cd. There is no division by current
PBR albedo. The implemented historical target is the compiler's lightmap colour
multiplier, not measured outgoing diffuse radiance. Unknown source transfer,
primaries and bake-time reflectance remain unknown; `Decode::Profile` is retained
at the API boundary for future supported operators, not used to invent a second
global calibration system.

## Geometry, footprints and the boundary witness

World coordinates are right-handed, +Z up. Normals point outward. Incoming
directions point receiver → source; spot/directional `Frame.forward` is light
propagation. Known frames must be orthonormal/right-handed. Surface and material
IDs use source BSP ordinal + 1, scoped by geometry identity. Observation IDs are
supplied from the pinned original-map/image/texel/surface/style identity; the
fixture protocol's local numeric IDs are not GPU atlas identities.

`ReceiverFromLuxel()` copies existing `SurfaceLightingObservation` geometry and
source surface identity without reading colour. It rejects padding, ambiguity,
moving surfaces, patches, unsupported chart states and nonplanarity. Original
positions are retained; compiler placement certification must be supplied
separately. A small numerical UV mapping bound is distinct from a curved geometry
envelope. The current compiler subset requires coincident geometric/shading
normals, known planar placement and mapping bound ≤0.002 world units; it does not
pretend to replay patch/seam interpolation. The latter tolerance is a mapping
admission bound, not a universal one-byte lighting guarantee.

Compiler surface super1 evaluates the explicitly pushed **centre**, not the
entire footprint. Grid reduction uses the supplied grid position without surface
push. The physical reference uses caller-leased, disjoint, UV-clipped world
triangles; it never derives a square from nominal sample size or invents padding.
It clips planar caster projections before response integration, preserving a
moving integration boundary. Average visibility and average irradiance are
reported as different quantities. Sky is not inferred from empty space.

General historical visibility calls the existing `VisibilityProvider::Query`
with conservative material policies. Unknown blockers remain unknown; the
entire aggregate is unavailable when a potentially contributing source has
unknown visibility. `externallyCertifiedVisibility` is an explicit caller
assumption and is marked as such, not a traced result. No PVS or region ID is
used as proof of optical visibility.

`CompilerTrace` requires complete opaque/sky world-geometry coverage explicitly.
It clips windings through the original BSP and subdivided trace tree with the
locked .25 clipping threshold, .1 node tolerance, .01 barycentric acceptance,
.25 determinant threshold, .5 inhibit radius and sky continuation. Unknown
alpha/translucent surfaces refuse this replay; use conservative static queries
instead. Original solid-origin classification is retained separately. A solid
termination without supporting geometry stays unknown. Sky reached before the
outside solid leaf remains sky in the scoped compiler policy.

The verified witness requires the original multi-sun BSP hash **and** complete
trace-input digest, 395 nodes/764 triangles, its exact two sun configurations,
normal push, encoding and 1,225 interior floor receivers. Changed geometry,
source configuration or output cannot inherit its verified scope. It includes
all 35 formerly excluded boundary texels: max byte error **0**. The old analytic
centre box prediction differs by up to 28 there. No universal two-unit band,
shadow dilation or hand-tuned offset is used.

The independent thin-door fixture exposed a separate failure: 25 nominal floor
UV positions are inside its one-unit wall. An initial incorrectly admitted
prediction differed by up to 95 RGB8. The evaluator now reports unsupported
compiler receiver placement there. Its other 1,200 samples match byte-exact.
This is **partial support**, not a passed full-fixture replay. Compiler solid
nudge must be implemented/verified before accepting those positions.

## Public API, ownership and numerical fitting hook

Public operations are `NetRadiantProfile`, `Require`, `Capability`,
`ReceiverFromLuxel`, `Encode`, `CpuForwardEvaluator::Evaluate/Prepare`,
`PreparedFactors::Records/Key`, `CompilerTrace::Build/Query/ClassifyOrigin`
and `PhysicalReference`. The evaluator reads no observed RGB, ground truth,
files or hidden evaluation views. An automated negative test changes all target
colours and verifies that its request is identical. Existing solver-view/process
isolation still governs what the separate optimizer may read.

```cpp
Bake::CpuForwardEvaluator forward;
auto profile = Bake::NetRadiantProfile(explicitRecipe);
Bake::Options options;
options.allowAssumed = true; // explicit candidate profile, retained in diagnostics
options.output = Bake::Domain::EncodedRgb8BeforeQuantization;
Bake::Result prediction;
auto error = forward.Evaluate(profile, candidateSourceLease, receiverGeometryLease,
                             decoderPolicy, geometryContext, options, readContext, prediction);
// Check error, each available/status/fidelity/issues and coverage before comparing.
// The fitting controller may compare TRAIN colours; this call never receives them.
```

The standalone `lightrecon_bake_forward_tool` is a small external-amplitude-test
entry point using the production implementation. Its `JKBAKE2` text request
contains recipe, source XYZ/type/compiler RGB, geometry-only receiver records,
BSP trace topology and caster triangles. It returns encoded/prequantization or
requested separate observation predictions and work counters. The trusted
`validate_cpp.py` controller creates these requests and compares the responses;
target RGB/answers remain outside C++. `--encode` tests the packing stage and
`--status` exercises real evaluator failure paths. This fixture protocol is not
a general stock-map material parser or a production archive replacement.

Requests borrow immutable owning `Chunk` leases. Results own their prediction
buffers and can outlive the evaluator/request. Prepared factors and acceleration
data are immutable; the evaluator has no shared mutable scratch, automatic
rebuilds, global ray cache or background jobs. The caller must keep providers and
geometry immutable and update their generation keys when data change.

Optional factors store distance, response geometry, cone/form factor, incoming
direction and visibility for a **bounded block**. They exclude amplitude but pin
profile, source IDs/pose/shape/units, receiver geometry and context generations.
Source motion or scene changes cause `InvalidState`; they do not reuse a stale
visibility mask. FAST envelope/cutoff is recomputed from current amplitude.
The linear law, active cutoffs, spot threshold and shared-peak encoding mean a
full source-to-RGB8 model is not globally linear. `globallyLinearInAmplitude`
describes a pre-encoding contribution branch only, not the encoded observation.
No gradient is advertised for frozen visibility or moving shadow boundaries.

Each call limits pairs, expanded rays, total traversal work and output bytes,
checks cancellation, and bounds source/receiver counts. Defaults are 4,096
sources, at most 1,048,576 receivers/pairs, 1,048,576 rays and 32 Mi work units;
applications should normally use much smaller tiles. No dense source×map matrix
is mandatory. `ReadContext.maxBytes` checks principal allocated blocks, not a
process-wide allocator cap/RSS promise. Reference polygon fragments and adaptive
depth are bounded separately. Split large requests deterministically; retain
each call's error and unsupported coverage. A work/memory/cancel error publishes
no partial `Result`. A contribution sink is a synchronous incremental stream;
its caller must discard a failed call's staged stream.

`available=false` has no numeric black interpretation. Status distinguishes
unspecified model, unsupported feature/domain, invalid input and unknown
visibility; `Error` distinguishes missing geometry/provider failure,
schema/input errors, stale factors, cancellation and memory/work exhaustion.
`issues` separately records assumed profile, conditional traces, unresolved
absolute scale, source-parameter uncertainty and unbounded approximation error.
Optical `visibilityIssues` remain distinct. Numerical error estimates are not
calibrated probabilities or guaranteed intervals; no confidence products are used.

## Results and costs

Machine-readable results live in `tools/rend2/bake_model/results/s01/`. Large
requests/responses stay under ignored `build/s01/`; no stock/game asset is added.

| Test | C++ / Python / compiler comparison |
|---|---|
| Original 20 direct-only BSP cases | 19 byte-exact; `colour_clip` max 1 byte; 24,500 floor samples |
| Original `multi_sun` | All 1,225, including the 35 boundary texels, byte-exact; old Python analytic box max 28, scoped Python replay and C++ both 0 |
| Point, spot, ambient deluxe | Max 0 byte in checked payloads |
| Unpacked point/linear/spot grid directed vs packed compiler | Max 0 in selected interior controls; reported with existing merge budget, not exact final packing |
| Original bounce1/bouncegrid1 | Direct-only discrepancy max 6; full bounce explicitly unsupported |
| Frozen X05 | All 14 required capability/positive/negative/encoding/boundary contracts pass; black and unavailable predictions do not satisfy positive contracts |
| Independent two-colour and oblique spot | Byte-exact |
| Independent gamma/clipped and saturated-white | Max 1 byte |
| Independent thin door | 1,200 byte-exact; 25 unsupported placement, complete-fixture success not claimed |
| Independent sky and coincident/split sources | Byte-exact prediction; split-source equivalence is not source-count recovery |

The one-byte clip differences occur at floating-point/shared-peak conversion
boundaries; double accumulation is packed through canonical float storage before
the byte stage. The predeclared one-code scope was not widened. Wrong linear
profile on inverse-square data has RGB8 RMSE 14.48 without fitting a gain;
changing attenuation while retaining the same cosine response has RMSE 17.72;
black RMSE is 24.91. Fresh repeated processes produce identical values, support
states, trace keys and work counts; timings/RSS naturally vary.

| SI witness (100 cd at z=4 m, receiver [-1,1]² m) | Value |
|---|---:|
| Centre-only | 6.25 lux |
| Independent solid-angle area average | 5.885750595 lux |
| C++ area average | 5.885764122 lux |
| Half-shadow C++ | 2.942885876 lux |
| Moving visibility derivative, analytic / C++ FD | -0.5 / -0.499993563 per metre |
| Irradiance derivative, independent / C++ FD | -3.290420112 / -3.290414810 lux/metre |
| Fixed 2×2 visibility derivative in that interval | 0 (incorrect moving-boundary gradient) |
| 2×2 m, 1 nit rectangle at z=4 m, coarse 8×8 C++ source rule | 0.215757087 lux; total error bound unknown |
| Same rectangle, existing Python 8×8 Gauss reference | 0.215480548 lux; C++ difference 0.000276539 lux |

These are separate SI fixtures, not a calibration of legacy BSP lighting. The
Python physical reference comparison records actual differences and numerical
limits, not compiler agreement in lux. The half-footprint reference used 28,699
smooth response evaluations; such integration belongs at selected boundaries,
not every ordinary map texel.
The initially requested rectangle receiver tolerance `1e-6` exhausted its
one-million-work limit and returned an error. The recorded finite-rectangle
example explicitly uses a coarser `1e-4` receiver tolerance; its total source-rule
error remains unbounded. No compiler acceptance tolerance was changed.

In the measured Release controlled run, compiler-trace build plus 1,225-sample
evaluation took roughly 0.01 s for point cases and at most 0.05 s for sky17.
The original room trace accounts for about 112 KiB owned acceleration data;
actual fresh-process peak working set was about 4.4 MiB. JSON records preserve
per-case work, time and OS-measured peak RSS. These are small controlled fixtures,
not a claim of measured whole-stock-map forward throughput.

## Reproduction and validation

From the repository root, with CMake/compiler on PATH (MSVC 2019 or C++11 GCC):

```powershell
cmake -S tools/lightrecon_test -B build/s01-standalone -G "Visual Studio 16 2019" -A x64
cmake --build build/s01-standalone --config Release --parallel 6
ctest --test-dir build/s01-standalone -C Release --output-on-failure
```

The local existing GCC standalone directory is `build/lightrecon-disk-standalone`;
the existing combined MSVC directory is `build/lightrecon-core-renderers`.
Exact commands used for renderer and numerical checks:

```powershell
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_bake_forward_tool lightrecon_bake_forward_test --parallel 6
build/lightrecon-core-renderers/Release/lightrecon_bake_forward_test.exe build/s01/integral.json
python -X utf8 -m unittest discover -s tools/rend2/bake_model -p "test_*.py" -v
python -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/s01/final --x05-manifest tools/rend2/evaluation/x05/results/manifest.json --x05-compiler-dir build/x05/compiler-final --cross-map-dir build/x06-controlled-final --integral-result build/s01/integral.json
python -X utf8 tools/rend2/lightrecon_regression.py --output build/s01/shader-regression
```

CPU tests include original geometry adapter, empty batches, all supported
kernels, uncertain/invalid values, explicit physical calibration, negative/NaN
inputs, units, visibility, ordering, budgets/cancellation, cache invalidation,
unknown features, physical clipping/derivatives and negative compiler-trace
cases. The C++ checks do not disappear under `NDEBUG`. Python tests explicitly
skip compiled-artifact tests when required local files are missing; they never
call that a numerical pass. Standalone CPU tests are 18/18, Python bake-model
tests 24/24, and disabled-path GPU comparison compiles 27 programs/51 stages with
zero differences. The C++ forward executable performs 67 numerical/contract
checks; X05 acceptance tests pass 16/16 and shadow-footprint tests 27/27.
SP and MP Release builds pass. Existing MSVC `/arch:SSE2`
warnings on x64 remain unrelated to this module.

To recreate missing compiler artifacts, use the pinned executable with the
existing `compare_bakes.py`, X05 `run.py`, and X06 `controlled.py` recipes; their
README files specify fresh-directory commands and source/settings hashes.
`validate_cpp.py` rejects a changed original BSP hash. This S01 run compared
the existing pinned compiled artifacts; it did not claim to recompile every BSP
or run gameplay. No visual/runtime lighting validation is required for this
offline-only change, and none is claimed.

## Follow-up boundaries

Before broad stock-map fitting: implement/verify profile-specific receiver nudge,
light sampling/filtering and source expansion; refine supported trace scopes;
add authentic material/alpha policies; and retain competing stock bake profiles.
Before final grid prediction: replay ordered compression, styles and the exact
accumulation recipe. Before GI: implement a separate supported transport operator
without relabelling wrong-direct residual as bounce. Before native tubes, curved
or finite-angular reference evaluation: add independently validated geometry and
integration, not a silently substituted point/rectangle.

Future optimizers can already fit fixed-geometry amplitudes against the supported
operators, reusing factors only on valid branches. They must select on held-out
observations, rerun the strictest supported final value, report missing coverage
and model discrepancy, and avoid treating a successful forward prediction as
evidence that an emitter exists or is uniquely localized.
