# S09: automatic shadow evidence in bounded CPU source fitting

Status: offline, conditional hypothesis fitting. No runtime light, shadow-map,
LTC, fog, PBR, CSM, gameplay visibility or renderer cvar changes. S08 was committed
separately as `9510015d0`. The S09 starting HEAD also contains unrelated SSR work;
source hashes in the numerical witness identify this implementation precisely.

The new automatic stage operates on original, explicitly interpreted source RGB
and retained world geometry. It does **not** certify original Raven lights or
turn a gradient, a converged optimizer, or a feasible caster association into a
verified physical source. Checked stock JA maps remain `UnknownProfile`.

## Interfaces and ownership

* `lightrecon/shadow_evidence.{h,cpp}`: observed gradients, possible edge
  interpretations, BVH-local caster discovery, competing associations, uncertainty,
  bounded objective, reference probes and OBJ/TSV diagnostics.
* `lightrecon/fit_evidence.h`: immutable optional fitting objective. It accepts
  source geometry and fitted RGB, without any observation reader or held-out API.
* `point_refine.{h,cpp}` and `emitter_shape.{h,cpp}`: optional `shadow` and
  independently reported `shadowWeight`. Zero weight performs no shadow work and
  leaves the previous objective/dependency keys unchanged. Both use the additional
  objective for accepting bounded geometry proposals. Existing RGB-only Jacobian
  proposals remain proposals; no new analytical visibility gradient is claimed.
* `StaticVisibility::ExtractBox`: bounded BVH-local export of original whole
  triangles, their optical category and patch-error envelope. No replacement
  collision BSP, clipping of world geometry or global ray-pair cache is introduced.
* `lightrecon_shadow_test`: owned numerical scenes and actual S08 fitting calls.
* `lightrecon_shadow_inspect`: training-only `.jkobs` diagnostic, supplied point
  prior, no optimizer and no access to a source-answer file.
* `tools/rend2/shadow_fit/benchmark.py`: two production C++ repetitions, original
  compiled archive diagnostics, pinned X04 replay, resource and identity reports.

All acceleration data and evidence are immutable across calls. Query stacks,
local candidate sets and report buffers belong to the caller. Geometry changes
change the visibility certification key; source XYZ/frame/extent changes retrace
the source-dependent state. The reusable cache is the existing immutable BVH and
the S07/S08 geometry-factor infrastructure, not frozen visibility across motion.
There is no map-load hook and no consumer dependency.

## Observation facts versus interpretations

`Sample` keeps original page/texel/style/chart/surface identity, actual mapped
receiver geometry, optional leased original footprint triangles, source-domain
RGB, noise, mapping and filtering metadata. `Gradient` is a measured adjacent
RGB change. `Edge` is a possible interpretation of connected compatible gradients.
`Caster`, `Association` and `Prediction` are separate records. Oracle bindings are
an explicit, optional input used only by ablation D.

`Detect` requires a map fingerprint and explicit colour/profile-assumption key.
It never decodes unknown gamma itself. Unknown transfer or primaries, known
material changes, normal creases, seams, saturation, optical/moving uncertainty
and mixed geometry are retained as facts but excluded from usable shadow fitting.
Filtering, smooth falloff, spot-cone boundaries, multiple lamps and indirect
transitions remain alternative explanations. `castShadowConfidence` remains
unknown: there is no fabricated probability or product of heuristic scores.

The detector uses positive X/Y lightmap neighbours of the same page/style,
relative luminance contrast and an explicit noise threshold. A local shoulder
comparison records smooth-falloff ambiguity. Compatible gradients are grouped
using grid-local connected components, not all-pairs clustering. Material and
chart IDs alone cannot reveal an undisclosed within-texture albedo edge. The
adversarial identical-RGB material test is deliberately admitted as a **possible**
edge and counted as such in the confusion table below.

Observed location uncertainty is half the sample spacing plus mapping error and
a supplied known filter radius. Unknown filter radius is flagged, not assigned
a universal blur. This interval is not a Gaussian standard deviation or a source
position confidence interval. The representative receiver keeps its actual
leased footprint; a square inferred from nominal sample size is never substituted.

`Sample::Role` is explicit. An optimization objective refuses any sample marked
validation or held-out. This supplements the existing blind-reader boundary;
it is not an OS sandbox against a malicious caller who relabels private data.
The archive inspector reads only Train samples and reports withheld counts.
The controlled fitting test has disjoint RGB training, edge training and spatially
separated held-out receivers. Held-out RGB is evaluated only after parameters freeze.

## Caster and source association

For each possible edge and supplied source hypothesis:

1. Query a bounded edge/source ROI through the static BVH.
2. Cast a coarse normal/tangent fan **and** rays immediately on both sides of the
   measured transition. A test exposed that a coarse fan alone can miss a narrow
   shadow interval; the data-resolution-dependent near-edge rays fix that case.
3. Extract edges of relevant hit surfaces, excluding internal coplanar
   triangulation diagonals. Finite geometry endpoints and material identity are
   preserved. Neither PVS nor a region ID establishes visibility.
4. Project each finite caster edge from the source onto the receiver plane.
   Check finite segment ordering, orientation, support radius, actual blocked
   side/caster hits and the visible-side pattern. A projected line alone is
   insufficient. Opaque barriers continue to constrain the independent RGB model.
5. Retain source/caster alternatives within a declared cost slack; keep a null
   explanation and unassigned edges. There is no permanent greedy assignment.
   Wrong, unknown, attempted and omitted diagnostic associations are counted.

The fan, local triangle extraction, candidate attempts, retained diagnostics,
source count, memory and cumulative ray/work totals are bounded. Exhaustion
returns `WorkLimit`/`MemoryLimit`; it does not silently make a missing caster clear.
The current search can miss complicated silhouettes that none of the bounded fan
rays reaches. Such edges stay unresolved. It does not enumerate all world triangles
for every edge/source pair.

For a point source, an observed receiver edge and finite caster edge imply a
source **plane family** with a receiver uncertainty band (the L6 construction in
the localization research). This is not an XYZ estimate. Several independent
families and direct-light information are required to constrain more dimensions.
Finite-source families/projections are approximate centre diagnostics, not hard
point-source constraints or calibrated covariance estimates.

## Objective and numerical semantics

The geometry optimizer uses

\[
 L(\theta,Q)=L_{\rm RGB}(\theta,Q)+L_{\rm prior}(\theta)
             +\lambda_s L_s(\theta,Q).
\]

RGB keeps S06/S08 nonnegative amplitudes, encoding/censoring semantics and explicit
relative/physical unit contracts. Shadow fitting adds no negative light, free
per-luxel GI or implicit ambient source. The RGB amplitudes are refit by the
existing inner fitter; the shadow term assesses geometry and association with the
resulting colour. It is not a new joint chromaticity optimizer.

For observed edge j and feasible association a, let d be the signed displacement
between predicted and observed finite boundaries, b the receiver location band,
and H the declared Huber loss. The bounded geometric cost is

\[
 u=H(\max(0,|d|-b)/\max(b,10^{-6})),\qquad c_d=u/(1+u).
\]

Tangent disagreement, visibility-pattern disagreement at the observed transition,
known transition-width mismatch and weak source/delta-RGB chromaticity mismatch
are independently weighted. Per-edge cost is the smaller of a null-explanation
cost and the best feasible association cost; comparable alternatives are retained.
Costs are averaged per connected edge, so many correlated pixels on one edge do
not multiply its weight arbitrarily. The bounded distance cost retains a proposal
signal across the declared search radius without forcing remote correspondences.
Weights/slack/null cost are modeling policy, not statistical confidence.

An illuminated shadow can contain GI. The shadow term checks a **change of direct
visibility**, never requires its dark side to have zero total RGB. The strong-fill
test preserves the same geometric association. An unsupported direct/GI RGB
decomposition still limits the independent intensity fit; the shadow term does
not solve or conceal that discrepancy.

Three concepts remain separate:

| Operation | Meaning and support |
|---|---|
| Centre visibility | BVH binary centre rays; explicit ablation/approximation |
| Physical footprint | Actual supplied receiver triangles; planar point shadow clipping plus smooth response quadrature through `Bake::PhysicalReference` |
| Finite source | Rectangle source quadrature with exact supplied receiver pieces; open-cylinder side quadrature for tube visibility; numerical approximation remains flagged |
| Optimization surrogate | Explicit deterministic receiver/source samples; no claim that their binary derivative is the integral derivative |
| Compiler replay | S01's source-locked opaque/sky directional fixture only; general finite-source `CompilerScoped` association returns `Unavailable` |

The reported integrated geometric visibility and the integrated unit direct
response are distinct. The internal unit source is a mathematical basis kernel,
not a calibration of the observed source to candela/nits. `absolutePhotometryKnown`
remains false. Finite-source quadrature discrepancy is not a certified total error
bound. Tube side-only emission omits caps, as in S08. Source extent and frame are
required; no invented dimensions or historical angular law are inserted.

The final frozen source parameters receive `EvaluateReference` in addition to the
search objective. Centre/surrogate search is assessed again with the physical
footprint operator. Results keep `shadowDiagnostics` and `shadowReference`
separately; the search loss is not relabeled a compiler-faithful prediction.
Missing reference geometry leaves associations unresolved. No differentiability
claim follows from deterministic sampling or optimizer convergence.
Receiver geometry-error envelopes are retained and hashed: an approximate patch
receiver is not flattened into an exact plane by clearing its error. A physical
spot request also requires an explicit supported angular law; an unknown historical
spot profile is not silently replaced by a smooth PBR cone.

Normal offsets use the visibility API's guarded original-to-offset segment.
They do not jump through a thin surface before the ray starts. Unknown alpha,
translucency, unbounded missing support, patch envelopes, unknown sidedness and
relevant moving-model bounds remain unknown. Local clear geometry need not wait
for perfect region segmentation. A no-hit shadow ray is never interpreted as sky.

## Controlled numerical results

The test controller supplies source height 128 and Y=0, allows X within bounded
free space, and hides RGB amplitude from fitting. The true X is -32. A wall top
at Z=64 projects the observed floor boundary to X=32. A separate lower wall is a
wrong caster. One independent bright RGB training receiver leaves position and
amplitude confounded. Edge training spans Y=-4..4; held-out receivers use Y=±14
and X=36,45,65. Held-out prediction includes actual opaque-wall visibility.
Edge RGB includes the independently evaluated cosine/inverse-square falloff,
source colour and explicit fill. The half-unit analytical visibility ramp is a
detector fixture, not a claim of compiler supersampling or exact area-averaged
radiometry. The finite-source detector fixture likewise uses a declared ramp.
Strict integrated visibility and its derivatives are tested independently.

This is **fixed-height one-dimensional localization**, not recovery of arbitrary
3D source geometry. All A–E modes use identical RGB training and frozen held-out
evaluation. D alone receives the correct caster binding; E discovers it.

| Mode | Initial X=-52: final position error | Held-out MSE | Rays, including frozen shadow reference |
|---|---:|---:|---:|
| A intensity only | 20 | 0.0701161917633 | 66 |
| B centre visibility pattern | 20 | 0.0701161917633 | 700 |
| C footprint visibility pattern | 20 | 0.0701161917633 | 616 |
| D oracle edge/caster | 0 | 1.08310458922e-16 | 890 |
| E automatic edge/caster/source | 0 | 1.08310458922e-16 | 890 |
| Black predictor | no source solution | 0.0908573982187 | — |

From initial X=-44, E reduces error 12→0 with held-out MSE
`6.49862753679e-16` and 882 rays. From X=-20, B/C/D/E all reach X=-32;
E uses 870 rays. Thus binary/pattern-only refinement can work from some seeds,
but is not reliable across this declared set. Oracle versus automatic error gap
is zero **on these simple scenes only**. Fitted relative amplitude is 10000
within `0.0009766`; this is not a candela estimate. The receiver location band
still admits nearby positions even when the optimizer happens to land on truth.

The coloured two-source test contains partially overlapping illuminated regions
and two distinct transitions. Both automatic source/caster associations are
correct; the two cross-source alternatives are rejected. Coincident/nearby
same-colour candidates retain alternatives. No unique source count is inferred.
The machine-readable reported-pair confusion matrix is TP=2, FP=0, FN=0, TN=2
for those four source/caster pairs only. It is not an exhaustive world-triangle
classifier or a stock-map accuracy estimate. The geometric rectangle penumbra
width is 8 world units and the projected width is 8; integrated centre visibility
is 0.5. A wide rectangle and two separated points agree at that centre but differ
at an independent penumbra receiver. The tube check remains explicitly sampled.

| Reported pair's hidden controller label | Retained | Not retained |
|---|---:|---:|
| Correct source and caster | 2 | 0 |
| Cross-source alternative | 0 | 2 |

The following is a **case-level possible-edge admission matrix**, not calibrated
cast-shadow classification accuracy:

| Hidden case label | Candidate admitted | Excluded/no edge |
|---|---:|---:|
| Shadow present (point, two-colour case) | 2 | 0 |
| Not a shadow | 1 | 6 |

The admitted non-shadow is the deliberately undisclosed albedo case with identical
RGB and geometry. Known material, seam, transfer, saturation and normal-change
cases are excluded; a dark uniform receiver produces no edge. Every case retains
`verified_cast_shadow=false`. There is no demonstrated general-purpose albedo
classifier. Unknown/missing optical tests produce no informative clear match.

For a half-unit receiver footprint crossing the planar point shadow, the analytic
visibility derivative with respect to source X is +2/world-unit. Independent
central differences of the C++ integrated evaluator give:

| Step h | Derivative | Absolute error |
|---:|---:|---:|
| 0.1 | 1.99998855591 | 1.14440917969e-5 |
| 0.02 | 2.00004577637 | 4.57763671875e-5 |
| 0.005 | 2.00004577637 | 4.57763671875e-5 |

The nearby frozen centre-sample derivative is zero. Float world coordinates limit
smaller steps; none of these derivatives is asserted for compiler point sampling.
Rectangle penumbra, clear/blocked/window controls, one-sided material policy,
strong shadow fill, moving bounds, corruption of geometry identity, cancellation,
work and memory caps are exercised in the C++ suite.

## Original compiled archives and boundary regression

The trusted inspector processes original mapped `.jkobs` RGB and static visual
geometry from the existing pinned compiled fixtures. It sees only training RGB
and a supplied first point XYZ prior. It does not optimize source positions or
report source recovery. The two-colour diagnostic deliberately supplies only
the first point; it is not an all-source benchmark.

| Archive | Training samples | Withheld raw entries | Gradients | Possible edges / eligible | Matched / unassigned | Rays |
|---|---:|---:|---:|---:|---:|---:|
| point_room | 1545 | 733 | 60 | 7 / 4 | 0 / 4 | 84 |
| two_colour | 1545 | 733 | 32 | 6 / 2 | 0 / 2 | 42 |
| door_occlusion | 1634 | 905 | 144 | 30 / 25 | 3 / 22 | 573 |

Each archive contains 16384 derived entries; padding, unavailable geometry and
incomparable entries are reported separately. Matched edges are feasible
conditional hypotheses, not independently labeled compiler shadow truth.
Unassigned curved/filtered/smooth/ambiguous features are not silently dropped.
These counts are not stock JA shadow detection precision/recall.

The pinned multi_sun BSP is
`0ff603f3537a83b5ca6bc80eae344c6e697b37c356151667a0904c438c8157ba`.
The unchanged diagnostic preserves the former 35 texels / 105 channels and
28-RGB8 maximum centre-analytic discrepancy. The X04 clipped trace-tree,
dimensionless barycentric .01 and continued-sun-after-sky replay still matches
all 1225 floor samples exactly. The implementation does **not** transfer .01 to
physical geometry, finite sources, general BSPs, patches, alpha masks or Raven.
The regression retains original observations, rather than reinstating an ignore band.

The production bake comparison retains 20 supported compiled cases passing their
original tolerances; bounce/bouncegrid remain two explicit `UnsupportedBounceReplay`
cases. Colour clipping's predeclared one-byte bound remains unchanged. No new
compiler invocation or new bake recipe is needed for this regression.

## Budgets, measured resources and reproduction

Default limits: 262144 edge input samples, 65536 measured gradients, 4096 possible
edges, 16 source hypotheses, 8192 local triangles, 2048 caster association attempts
per edge, eight diagnostic alternatives per edge, 200000 rays, 16M work units and
64 MiB scratch/report policy. The implementation returns explicit limits rather
than claiming all combinations of these independent maxima fit simultaneously.
The `ReadContext` may reduce memory/cancel the work further. Actual peak RSS is
measured in the Python controller through Windows `GetProcessMemoryInfo`.

GCC Release repetitions of the complete 285-check owned suite took 0.35–0.36 seconds each,
peak RSS 4.41–4.42 MiB. Archive diagnostics took 0.30–0.98 seconds and 6.93–7.14 MiB.
The separate Python X04 replay took 0.81 seconds and 20.68 MiB. These are small
fixture costs on a shared machine, not a stock-map throughput guarantee. Numerical
values/IDs/work counters are byte-stable across two runs; time/RSS live separately.

From the repository root, with CMake/compiler and Python on PATH:

```powershell
cmake -S tools/lightrecon_test -B build/s09-tools -G "Visual Studio 16 2019" -A x64
cmake --build build/s09-tools --config Release --parallel 2
python -X utf8 tools/rend2/shadow_fit/benchmark.py --tool build/s09-tools/Release/lightrecon_shadow_test.exe --inspect build/s09-tools/Release/lightrecon_shadow_inspect.exe --out build/s09-repeat
python -X utf8 -m unittest discover -s tools/rend2/shadow_fit -p "test_*.py" -v
python -X utf8 -m unittest discover -s tools/rend2/shadow_footprint -p "test_*.py" -v
python -X utf8 -m unittest discover -s tools/rend2/light_localization_research -p "test_*.py" -v
```

`benchmark.py` defaults to the existing `build/x02/compiled-fixtures-final`,
`build/q3map2-bake-verified` and pinned X04 BSP paths; explicit overrides are
available. It checks original BSP/recipe hashes and refuses changed input.
Recreate absent fixtures using `rend2-q3map2-validation.md` and the existing X02
pipeline. It does not download or redistribute game assets. New output directory
must not exist. Output includes OBJ/TSV, original archive checksums, two CPU
repetitions, numerical witness and separately measured process resources.

Actual build/regression commands and final status are recorded in the versioned
`tools/rend2/shadow_fit/results/` witness and validation report. GPU/game visual
tests are not required for this CPU objective and were not performed.

Validation completed on 2026-10-30: all 25 GCC CPU CTest cases passed (24 in the
bounded parallel run, S07 separately in 305.54 seconds); the three selected MSVC
shadow/shape/visibility cases passed. SP and MP Release renderer targets built.
Python gates passed 3 tests, footprint/replay passed 27, localization passed 33.
Shader-input/binding comparison covers 27 programs and 51 stages and is a CPU
source/permutation comparison, not a GPU compilation or visual test. The versioned
`results/validation.json` records exact commands, result scope and log hashes.

Shadow algorithm/schema version 1 participates in evidence and objective keys.
Enabled fitting keys include objective identity and weight; geometry/material
interpretation changes invalidate the BVH certification dependency. The archive
and canonical runtime asset schemas are unchanged. A zero shadow weight does not
change the prior fitting key or require this stage. No recovered-light capability
is granted by a matching edge or an available diagnostic result.

## Deployment limits and next evidence

* No stock source-location truth, general historical Raven trace replay, or blind
  stock-map shadow localization success is established.
* Large curved casters and unknown masks stay uncertain. Association is bounded
  finite mesh-edge discovery, not arbitrary silhouette reconstruction.
* Filter width/source extent/albedo/GI/profile can be confounded. A broad transition
  does not uniquely determine source size. Multiple-source mixtures on one edge
  are retained as alternative explanations, not fully factorized here.
* Physical footprint integration is not a claim about q3map2 `-super 1` samples.
  The compiler-specific reference is deliberately separate.
* The demonstrated automatic improvement fixes height and Y. Before unrestricted
  XYZ/type/count publication, require independent receiver/caster planes, spatial
  held-out improvement, profile alternatives, finite-source quadrature convergence,
  source-removal tests, known-source compiled localization and calibrated feasible
  source sets. Better optimizer convergence cannot replace those tests.
* Strong GI does not falsify a blocked direct ray, but an unsupported bounce or
  material model still prevents calibrated source-intensity attribution.

Related verified scope and derivations:
`rend2-shadow-footprints-x04.md`, `rend2-light-localization-research.md`,
`rend2-bake-model.md`, `rend2-emitter-shapes-s08.md`,
`rend2-lighting-evaluation.md`. No external dependency or runtime renderer path
was introduced by S09.
