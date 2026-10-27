# X04: baked-luxel shadow footprints and source-motion derivatives

This offline reference experiment distinguishes three different values: physical
area-integrated direct lighting, a **specified compiler's actual sampling/trace
rule**, and an optimizer approximation. They are not interchangeable. No runtime
shadow, lightmap upload, visibility core, renderer consumer or solver is changed.
The source-locked [bake model](rend2-bake-model.md),
[localization research](rend2-light-localization-research.md),
[canonical luxel mapping](rend2-luxel-observations.md), and
[static visibility](rend2-static-visibility.md) remain the integration boundaries.

Tools are under `tools/rend2/shadow_footprint/`. Public self-authored observations,
CSV tables, compiler receipts and a boundary diagram are in its `results/`
directory. Private/large run products remain under ignored `build/x04/`.

## 1. Reproduction and root cause of the 35 texels

The original frozen `multi_sun.bsp` SHA256 is
`0ff603f3537a83b5ca6bc80eae344c6e697b37c356151667a0904c438c8157ba`.
It is checked against the unchanged `tools/rend2/bake_model/validation.json`.
The pinned executable is NetRadiant `2.5.17n-git-d9338309e`, revision
`d9338309ea8fab75027b7382efaabe034302b862`, binary SHA256
`894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4`.
The original tolerances and two-world-unit **diagnostic exclusion** were not
modified before reproduction. The same 22 fixtures were freshly compiled.

The fixture is a sealed, self-authored room with inner walls x/y=±384, floor z=0,
sky ceiling z=400. Sun1 has incoming direction +Z and compiler brightness120;
sun2 has incoming direction `(1,0,1)/sqrt(2)`, red, brightness40. No finite sun
jitter is requested. The recipe is `-game ja -threads 1`; BSP
`-meta -leaktest -noflares -samplesize 16`, VIS, LIGHT
`-deluxe -deluxemode 0 -gamma 1 -compensate 1 -exposure 0 -bounce 0 -super 1
-nocollapse`. Compiler-relative brightness is **not** physical lux/candela.

All 35 observations are page0, x23, y7..41: world x=-16, z=0, y=-272..272 in
16-WU increments. Their UV-derived covered footprint is256 world-unit², with
geometric/shading normal +Z. The compiler's sample is at z=1, while the original
observation stays on z=0. The unshifted geometric oblique-sun boundary is x=-16;
the centre-ray analytic prediction with sample push1 has boundary x=-15. The
old analytic formula therefore predicts `[148,120,120]` on this whole row.

Actual compiler values are `[120,120,120]` on17 texels (negative Y), and
`[148,120,120]` on18 (Y≥0). Thus105 RGB channels were formerly unchecked;
17 red channels differ by -28, the other88 channels agree. Sum absolute residual
is476 encoded byte units. This is an L1 sum of **encoded differences**, not
physical missing energy. The two-unit mask contains35 texels but only17 actually
mismatch. All affected coordinates, positions, UV-clipped polygons, normal data,
sun directions, signed residuals, source geometry and clipped trace-hit triangles
are exported in `results/boundary.json` and `boundary.csv`.

### The tested cause is trace-tree geometry plus barycentric tolerance

Pinned `light_trace.c` filters world triangle windings through the original BSP,
subdivides trace leaves, clips the windings and triangulates the resulting
polygons. `TraceTriangle` accepts barycentric coordinates down to -0.01 and up
to1.01 (`BARY_EPSILON`), with determinant/coplanar threshold .25. The epsilon
is dimensionless: its world-space width changes with each clipped triangle.
There is no universal world-space boundary dilation.

The oblique ray from x=-16,z=1 intersects ceiling x383,z400, then the x384 side
wall's plane at z401. Sun tracing sets `testAll=true`: sky stacks `C_SKY` and
does **not** terminate the subsequent opaque tests. Some clipped wall triangles
admit the z401 intersection through their barycentric tolerance. Their leaf
membership and post-clipping triangle size differ across Y. This explains the
apparently irregular half-row transition without attributing it to a luminous
area source or applying a fitted blur.

The independent Python replay implements this **restricted opaque/sky trace-tree
subset**, using the locked source constants. It constructs395 world-tree nodes
and764 triangles. The native verbose compiler reports396 nodes including its
additional empty skybox node, and764 triangles. The replay reproduces all1,225
tested interior floor RGB samples byte-exactly, including all35 formerly
unchecked texels. Checking all original uncut triangles with the same epsilon
does **not** reproduce this result. Clipping/tree traversal are essential.

| Clipped-tree BARY epsilon | Max boundary error | Sum absolute RGB8 error |
|---|---:|---:|
| 0 | 28 | 476 |
| .001 | 28 | 476 |
| .005 | 28 | 140 |
| source value .01 | 0 | 0 |

This sweep tests the source-defined mechanism; .01 was not fitted. A representative
blocked ray hits a wall fragment with one barycentric component=-.005 at z401.
Across Y=16 and beyond, the relevant clipped/visited triangle set instead admits
only sky. Removing subdivision (`-lomem`) makes all35 samples dark to sun2.
The exact trace result is triangulation/compiler-policy dependent, rather than
an ideal opaque-wall physical visibility boundary.

This is not an instrumented compiler, bit-for-bit emulation of all trace floats,
or general replay of alpha masks, patch subdivision, portal skies, entities,
inhibit radii, all BSP leaf cases and arbitrary sample nudges. Equality on this
fixture is a scoped verification. Historical Raven behaviour remains
`UnknownProfile`; no original production compiler is identified.

## 2. Sampling/filtering paths actually exercised

All source paths below refer to the revision/hash in `source-lock.json`; local
LF-normalized source hashes are checked during boundary reproduction.

| Mechanism | Pinned source location | Fixture consequence |
|---|---|---|
| Axial UV mapping and luxel origin | `lightmaps_ydnar.c:682`, `:773`; `light_ydnar.c:443` | Origin from lightmap vectors, not arbitrary draw-vertex position; original UVs have the atlas half-texel convention |
| Normal and sample push | `light_ydnar.c:527`, `:615`; `q3map2.h:271` | Default one-unit push along projection axis with normal sign; floor +Z. Normal averaging/perturbation exists, but floor normals are constant |
| Invalid-cluster nudge | `light_ydnar.c:640..682` | Cluster-filter test, fixed tangent offsets and vertex fallback. These interior floor points need no inferred nudge |
| Initial one-sample evaluation | `light_ydnar.c:2234..2283` | One ray/luxel/source at super1 with default lightSamples1; no finite receiver-area integral is implied |
| Adaptive/alpha sampling | `light_ydnar.c:1731..1967`, `:2290..2377` | `SubmapRawLuxel` derives neighbour vectors; recursive2×2 stamps and contrast/alpha triggers. Not active in control |
| Source jitter/order | `light.c` sun contribution around `:1045..1159`; `shaders.c` sun parsing | Two declared singleton suns; no `sunExt` jitter, no random samples, no bounce or area sources. Addition precedes output encoding |
| Ray setup and sky continuation | `light_trace.c:1723..1779`, `light.c:1140..1155` | `testAll`, sky flag requirement, subsequent opaque hit rejects sun |
| Winding clipping / leaf subdivision | `light_trace.c:418..617`, `:622..891`, `:1300..1340` | .25-WU winding-plane classification; .1-WU traversal split tolerance; adaptive clipped triangles |
| Triangle intersections | `light_trace.c:1393..1532` | Bary .01, coplanar .25, self-shadow exemption within .5 only for matching source surface; no blanket world-space shadow width |
| Per-light filtering | `light_ydnar.c:2210..2225`, `:2420..2510` | Radius in luxel units, outer X/Y weights .5 (corner .25), valid clusters renormalized. No filter in control |
| Superluxel downsampling and packing | `lightmaps_ydnar.c:2537..2650`, `:3287` | Average valid superSample² records; control super1/nocollapse; padded/stiched values are not independent receiver geometry |
| Output colour | Existing `bake_model.encode_color`, locked `ColorToBytes` | Gamma1/compensate1/exposure0, RGB8 truncation, no peak clipping at120..148. No renderer gamma/tone mapping |

Seven **new recipes**, each independently baked, isolate boundary sensitivity;
the changed recipes are not held to the original single-point formula's tolerance:

| Recipe | Original seam-row compiler values/counts |
|---|---|
| Control | 120×17,148×18 in red; green/blue120 |
| `-lomem` | 120×35 |
| `-super 2` | 134×35 |
| `-samples 2` | 131×17,136×18 |
| `-filter` | 127×15,130×2,137×1,141×17 |
| Shader sample offset0 | 120×35 |
| Shader sample offset2 | 120×17,148×18 |

For example super2's134 is consistent with averaging two lit and two unlit
subsamples; it does not establish a universal area PSF for other compiler modes.
No filter/jitter/finite source is needed to explain the original control mismatch.
The remaining unknowns are general replay scope and Raven, not an unexplained
28-byte error in this controlled room.

## 3. Receiver footprint and reference direct-light integral

For observation q, let its original UV square be `[x/W,(x+1)/W] ×
[y/H,(y+1)/H]`. Intersect it with **every actual source UV triangle**. Map each
disjoint piece through that triangle's affine position/shading-normal map and
integrate with world-area Jacobian `|dp/du × dp/dv|`. This matches the footprint
construction in `surfaces.cpp::MapLuxels`, whose public summary retains bounds,
covered fraction, area and flags, but not its temporary clipped polygons.
The Python reference reconstructs those polygons from the existing compiled
fixture's original drawVerts/indices/UVs using the same conventions. It does
not reconstruct square world footprints from `samplesize16`, use GPU atlas
offsets as identity or replace the map-load capture pipeline.

`geometry.py::footprint` rejects positive-area overlaps with incompatible charts,
degenerate UV/world triangles and conflicting duplicate normals/material roles.
Exact duplicate references count once. Shared edges have zero measure. Partial
coverage remains partial and is normalized by **covered physical area**; uncovered
padding has no prediction. Multiple triangles preserve piece normals and area,
not independent duplicate RGB targets. UV seams, style/channel and filtering
eligibility must remain associated with the original observation. A filtered
atlas padding value has no invented dense world receiver. The tiny reference
does not implement general multi-chart lightmap stitching or compiler padding.

Quadratic receiver patches are approximated only with explicit affine UV control
grids and a conservative control-hull derivative envelope. Sampled geometric
normals and interpolated shading normals stay distinct. The reference returns a
mesh prediction **plus a nonzero geometry envelope**, not a certified curved
surface result. True curved visibility bounds require robust ray/patch envelope
queries. Unsupported UV patch maps are unavailable, not flattened.

Define covered receiver area A, source measure dμ and binary or bounded optical
transmission V. The two quantities of interest are

```
Vbar = 1/(|A| μ(S)) ∫A ∫S V(x,s) dμ(s) dA(x)
F_rgb = 1/|A| ∫A ∫S V(x,s) G_rgb(x,s) dμ(s) dA(x)
```

For point light, `G = I_rgb max(0,n·u)/r²`, u receiver→source. For a rectangle,
`G dμ = L_rgb max(0,n·u) max(0,n_source·(-u))/r² dA_source`.
For directional uniform-radiance cone, integrate `L_rgb max(0,n·u)dω`;
`L=E_perp/(π sin²α)`. α=0 is a directional delta with specified perpendicular
illuminance. The source axis is propagation; incoming direction is its negative.
Spot fixtures use an explicitly declared smoothstep between inner/outer cone
cosines, not an alleged historical Raven spot profile. Rectangle extent is full
width/height, and the frame is right-handed/orthonormal.

Analytical fixtures use metres, point/spot candela, rectangle luminance cd/m²,
directional perpendicular illuminance lux and scene-linear RGB channels whose
luminance carries the stated photometric scale. No BSP compiler scalar is
converted into these SI values. No alternative renderer calibration is added.

`Vbar * G(centre)` is not generally F. Neither is average V times average G:
visibility and geometric response correlate, particularly near sources/edges.
The tests include a nontrivial footprint with source falloff varying across the
visible subset and assert that the centre-product approximation differs.

### Reference implementation

For the supported planar geometry, project each opaque caster triangle onto
each receiver plane from the current source sample. Convex clipping/subtraction
computes a disjoint visible polygon union. Integrate smooth G on it using2/4-order
Gauss-Duffy estimates and bounded adaptive subdivision. Visibility area is exact
up to floating-point clipping; contribution is a numerical reference, not a
globally certified cubature theorem. Difference of nested rules is labelled an
**error estimate**. A narrow singularity or unmodelled geometry can defeat it.

Finite rectangle emitters use Gauss source-area quadrature; finite angular suns
use Gauss cosθ and evenly spaced azimuth. A single order's estimate does not bound
source integration error. Compare independent orders and independent joint4D
quasi-Monte Carlo point rays; retained disagreement limits derivative accuracy.
This prototype projects every small fixture triangle. Production work must use
the existing BVH to gather potentially relevant casters, never O(all triangles)
for every luxel. No new general-purpose acceleration structure is introduced.

Caster/source-plane crossing, grazing projection or non-enclosing sky order is
explicitly uncertain; it does not turn unknown geometry transparent. Opaque
coverage can still prove zero upper contribution even with global incompleteness.
Ordinary sky is not empty space: a sun/sky request requires a known sky polygon
and verified ordering. General non-enclosing/multiple sky ordering is unsupported.

## 4. Independent exact value and source-position derivative

The reference test has horizontal footprint x[1.3,1.4], y[-.05,.05], point source
s=(0,0,H), H=4, and wall x=w=1 with top h=1. It has visible x>b where

```
b = (H w - h sx)/(H-h)
Vbar = clamp((x1 - b)/(x1-x0),0,1)
```

For a horizontal rectangle, integrated `I cosθ/r²` equals I times its solid
angle. If xL=max(x0,b), then

```
K(x,y) = atan2((x-sx)(y-sy), H sqrt(H²+(x-sx)²+(y-sy)²))
F = I/[Δx Δy] * [K(x1,y1)-K(xL,y1)-K(x1,y0)+K(xL,y0)]
```

`analytic.py` differentiates this **closed form including moving xL**, using tiny
forward-mode dual numbers. It is independent of polygon clipping and ray
sampling. At edge/footprint topology events the derivative can be one-sided or
undefined; clamp branches must be reported, not averaged into fake smoothness.
The smooth point/spot AD tests separately differentiate geometry/angular response
with visibility fixed. This validates smooth factors, not shadow derivatives.

More generally, on a regular shadow branch `V=H(g(x,θ))`:

```
∂θ F = 1/|A| [ ∫visible ∂θG dA
                 + ∫shadow-boundary G (∂θg / |∇A g|) dℓ ]
```

Freezing visibility loses the second term. A finite deterministic point set
gives zero derivative of average binary visibility almost everywhere; its
determinism does not make that derivative physical. Central differences with
fresh independent sample sets can amplify noise as1/step. The benchmark records
fixed sample sets, recomputed stratified sets, frozen-visibility smooth AD and
retraced integrated differences at steps .1,.01,.001,1e-4,1e-5m.

Numerical value/gradient tables and measured timings follow below after the
frozen run. They must be read together with finite-source order convergence,
not as certification of all sampled derivatives.

## 5. Finite sources and optical uncertainty

For a uniform horizontal source full width D at height H above a straight caster
top h, geometric penumbra support is `D h/(H-h)`. For directional angular support
[-α,+α] perpendicular to a straight edge it is `2h tanα`. These are support
widths, not10–90% irradiance widths and not footprint/filter widths. Independent
source/receiver geometries are required to separate extent from filtering.

The suite includes rectangle .6×.4m at z4, angular sun radius .03rad, spot next
to an occluder, caster heights .5/1/2/3m, thin walls, narrow openings, near-tangent
rays and two independently positioned sources. For rectangles an independent
closed form integrates the clamped visible fraction over source width, and
independent4D quasi-Monte Carlo rays check both irradiance and visibility.
RGB source-area and receiver-area errors remain distinct. Differentiating a
finite set of source quadrature nodes can still alias an emitter-side boundary;
source-order agreement is required in addition to finite-difference-step tests.

For alpha-tested unknown masks, translucent, unsupported or moving blockers,
the implementation computes lower/upper visibility/contribution using the
possibly-blocked footprint, instead of silently sampling it clear. With
unbounded missing geometry, lower visibility is zero. A known opaque wall still
removes its shadow from **both** bounds. Geometry confidence is `None`, never a
product of arbitrary confidence factors. For unsupported projection/curved
receiver envelopes, upper irradiance can remain unknown rather than falsely
bounded by the approximate mesh's value. Numerical estimates pertain to the
specified mesh only, not unknown bake-time geometry.

No physical observation is displaced to hide self-intersection. Independent ray
fixtures use explicit tiny start exclusion and distinguish coplanar ambiguity.
The compiler replay uses its source-defined self-shadow/inhibit rules separately.
Any nonzero ray bias belongs to a declared query policy; the original world
position, UV identity and area are unchanged. Very thin walls must not disappear
because of a blanket normal offset.

## 6. Limited localization experiment

`localize.py` fits only source height H∈[2,8]m and one nonnegative intensity, with
known axis/count/profile and true H4/I8cd. This is not an unknown-XYZ inverse
solver. Near-source coplanar brightness is quantized to1/2048lux. Additional
training shadow footprints have a known wall x1/h1; held-out receivers use
a **different wall x.8/h1.5**. An explicit edge residual uses an independent known
caster/receiver edge, not the same RGB measurement counted twice as independent.

Four initial heights2.2,3,5.5,7.5 test intensity-only, centre-ray visibility,
integrated visibility and an explicit boundary term. All trial methods fit the
same amplitude bounds, use the same bounded local descent/line search and are
reevaluated with the strict area-integrated held-out model. The explicit-edge
case has additional independently supplied geometric evidence and is not a
claim of equal information budget. Its .005m edge tolerance is a declared
synthetic measurement assumption, not a stock-map confidence estimate.

Integrated visibility fixes one centre-model bias and gives a valid local
boundary derivative, but does not remove flat fully-dark/fully-lit basins.
Matched shadow correspondences can initialize across those basins. The report
includes failures from distant starts and does not equate convergence frequency
with source existence probability. Unknown edge correspondences, albedo/GI,
historical profiles and source count remain unresolved outside this experiment.

## 7. Reusable contract and performance policy

The CPU Python prototype exposes `api.evaluate(footprint, source, scene, profile,
Policy)` and `position_jacobian(..., step)`. They return owning value records:

* Original observation/source IDs, source configuration hash, scene identity,
  selected profile/domain/units; no shadow-map or GPU references.
* Integrated RGB lower/upper, area/source-measure visibility lower/upper,
  numerical error **estimate**, optical/geometry reasons, source integration
  order, work counters (ray queries, projections, response evaluations, pieces).
* Differentiability/method status; Jacobian columns when numerical differences
  are requested, step and total work. Unknown optical intervals have no unique
  derivative and explicitly return unavailable.
* `coarse` point-query approximation versus `reference` planar boundary clipping.
  Unspecified historical profiles return no fabricated RGB. `PhysicalSI-Direct-v1`
  must never silently substitute for `UnknownProfile` or q3map point sampling.

No softened-visibility model is required/installed here. Centre sampling and
arbitrary numerical smoothing may be optimizer proposals only. If future
continuation uses soft visibility, label its width/rule separately and accept
final candidates only after retracing the appropriate stricter physical/**bake**
forward model with its own uncertainty. Never smooth target RGB to obtain a
convenient gradient or call an optical interval a posterior probability.

A production adapter should use the existing X03 `Bake::SourceHypothesis`,
`AmplitudeDomain`, `Decode::Profile`, `Photometry` and owning chunk/provider
contracts. Transient receiver pieces come from the immutable original surface
snapshot and its canonical luxel ID. Compiler-relative Q cannot be cast to
canonical photometry. The Python SI fixtures are a research request/response
example, not a second global calibration contract or persistent observation
format. No `.jkobs`/asset schema or cache entry is reinterpreted by this task.

Cache UV-clipped receiver pieces, geometry BVH and source-independent quadrature
rules. Do not cache source-dependent visibility without a complete source pose,
shape, scene, bias and profile key. The prototype caches only Gauss rules globally
and local adaptive sample values inside one evaluation. Source motion creates a
new key and retraces/reprojects every affected contribution.

Production strategy: inexpensive certified clear/blocked BVH tests first, smooth
response quadrature for clear strong gradients, boundary-aware refinement only
where the geometry and footprint indicate a crossing, high accuracy for final
fits/held-out checks. Equal sample values are **not** proof of no boundary: the
negative test shows a .0001m doorway entirely missed by point-query adaptive
refinement despite a positive exact projected aperture. Do not spend millions
of rays on every fully visible/blocked luxel or create a global ray-pair cache.

Work/piece counts are explicitly bounded; exhaustion returns `WorkLimit`/no
partial successful lighting through the API. Numerical recursion has a depth
limit with `NumericalLimit` status. Full fixture geometry is intentionally small;
the reference Python triangle loop is not a measured map-scale native BVH path.

## 8. Reproduction and validation

Python standard library only. Use a new output directory for each compiler or
experiment run; no previous goldens/results are overwritten. From repo root:

```powershell
python -X utf8 -m unittest discover -s tools/rend2/shadow_footprint -p 'test_*.py' -v
python -X utf8 tools/rend2/light_localization_research/test_shadow_localization.py -v
python -X utf8 tools/rend2/bake_model/test_kernels.py -v
python -X utf8 tools/rend2/bake_model/test_identifiability.py -v

$q3 = Resolve-Path build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe
python -X utf8 tools/rend2/bake_model/compare_bakes.py --q3map2 $q3 --output build/x04-new-compiler
python -X utf8 tools/rend2/shadow_footprint/compile_variants.py --q3map2 $q3 --output build/x04-new-variants
python -X utf8 tools/rend2/shadow_footprint/compiler_boundary.py --bsp build/bake-model-comparison-final/authoring/base/maps/multi_sun.bsp --output build/x04-new-boundary
python -X utf8 tools/rend2/shadow_footprint/benchmark.py --output build/x04-new-benchmark
python -X utf8 tools/rend2/shadow_footprint/report.py --benchmark build/x04-new-benchmark/result.json --boundary build/x04-new-boundary/boundary.json --compiler build/x04-new-compiler/result.json --variants build/x04-new-variants/result.json --output build/x04-new-report
```

The compiled-fixture test explicitly skips if the original frozen BSP is absent;
this is not a pass for the boundary requirement. Recreate fixtures with the
[existing compiler workflow](../tools/rend2/bake_model/README.md), retain the
versioned source/input recipe and check the original BSP hash before claiming
that exact reproduction. Fresh bakes may contain unversioned metadata strings;
do not silently bless a different hash as the original golden. `compile_variants`
requires the exact binary hash. No game/stock assets are needed or redistributed.

## 9. Recorded results and remaining scope

Recorded2026-10-27 on `rend2-unified-wip`. Final results are generated from
`build/x04/verified/result.json`, `boundary-accepted/boundary.json`,
`compiler-all22/result.json`, `variants/result.json`. An identical numerical repeat
is under `build/x04/verified-repeat/`; wall-clock/RSS are separate, non-deterministic
measurements. Early `benchmark-v1/v2`, `accepted`, `final`, `frozen` directories
are exploratory and not the final public witness. Source hashes pin the final
experiment and the runner refuses publication if Python files change mid-run.

### Value/cost comparison

The independently exact value is .03530417704076462lux and visibility2/3. Fixed
sampling gives oscillating convergence rather than a guaranteed monotone gain.
These are one-run timings on the local Windows Python3.12 interpreter, not native
engine/BVH throughput. Two benchmarks were run concurrently; timings include that
contention and should not be used as production speed estimates.

| Method | Visibility error | Lux error | Actual point rays | Smooth response evaluations | Approx. time |
|---|---:|---:|---:|---:|---:|
| Centre | +.333333 | +.0178585 | 1 | 1 | .3ms |
| Fixed4×4 | +.083333 | +.00445106 | 16 | 16 | 2ms |
| Fixed16×16 | +.020833 | +.00111194 | 256 | 256 | 32ms |
| Shifted Halton256 | +.001302 | +.00006961 | 256 | 256 | 41ms |
| Point-query adaptive | +.012166 | +.00064967 | 968 | 968 | 123ms |
| Boundary clipping + adaptive smooth integral | <5e-15 | <5e-17 | 0 | 10,220 | 0.57s |

Boundary clipping performs four projected triangle tests and four piece-accounting
operations rather than point rays. Its deliberately tight1e-11lux requested
tolerance costs many smooth evaluations. Lower tolerances/analytic special cases
are appropriate for coarse work. Exact clipping does not mean zero computational
cost. Its nested-rule estimate is3.59e-12lux; it is explicitly not a certified
bound. For a declared fixed analytic exposure of4000 encoded units/lux (not JA
calibration), centre error is71.43 byte units, fixed4×4 is17.80, Halton256 .278,
adaptive2.60 and reference negligible. No arbitrary best-fit exposure normalizes
these differences away.

Boundary-aware10/50/90% footprint transition positions are1.293333/1.333333/
1.373333m, width .08m. Centre sampling shows zero transition width; fixed4×4
gives .075m, fixed16×16 .08125m. Their leftmost50%-threshold convention gives
edge biases -.0125m and -.003125m respectively because of staircase plateaus.
The true point-source penumbra is zero: this transition is receiver-area
integration alone. Do not infer source extent from that width.

### Derivative accuracy and failure cases

For motion in source Z, the exact integrated derivative is
**+.044405379076956lux/m**. The smooth/frozen-visibility32×32 derivative is
**-.014647007773lux/m**, with the wrong sign. The five-point-grid estimator's
visibility derivative is zero for small steps, but its smooth response still
changes; total sampled F therefore has an incorrectly negative derivative.

| Central step, m | Boundary-aware FD | Error against exact derivative | Fixed5×5 FD | Fresh independent8×8 strata FD |
|---|---:|---:|---:|---:|
| .1 | .0446086363 | +2.03e-4 | .0378116610 | .0477454340 |
| .01 | .0444074089 | +2.03e-6 | -.0133739343 | -.0570805311 |
| .001 | .0444053994 | +2.03e-8 | -.0133738423 | -.4341841830 |
| .0001 | .0444053793 | +2.01e-10 | -.0133738414 | -8.373298262 |
| .00001 | .0444053791 | +6.56e-12 | -.0133738414 | -83.59796180 |

X/Y derivatives, fully visible and fully blocked branches, and multiple-source
translations are in `derivatives.csv`/`benchmark.json`. The two-source reference
has value .13511646743405lux; simultaneous XYZ translation FD errors at1e-4m
are below1.8e-10lux/m. Smooth spot-orientation AD at the receiver gives
-.274368334451lux/rad; independent FD error decreases from4.21e-4 at .01rad to
4.21e-10 at1e-5rad. This checks smooth angular response only, not moving
occlusion. At a clamp/topology event the smooth derivative formula is not valid.

These finite differences have an independent closed-form reference. In contrast,
finite-source derivatives below do **not** acquire correctness just by becoming
deterministic or agreeing at several step sizes.

### Finite-source convergence

| Rectangle source Gauss order | RGB-channel illuminance, lux | Visibility | Error vs exact visibility .5833333333 |
|---|---:|---:|---:|
| 2 | .0068444720 | .544658199 | -.0386751 |
| 4 | .0075430044 | .606523223 | +.0231899 |
| 8 | .0072469397 | .582584674 | -.00074866 |
| 16 | .0072581604 | .583305674 | -.00002766 |
| Independent joint4D Halton16,384 rays | .0072613977 | .583557129 | +.00022380 |

The high-order/QMC value disagreement is3.24e-6lux; it is an observed convergence
diagnostic, not a rigorous error interval. Rectangle width derivative of
visibility has exact value approximately-.138888889/m. Source orders4/8/16 at
step1e-4 give -.184764159/-.137385841/-.141590276. Their excellent **step** stability
would conceal remaining source-quadrature bias. Irradiance width derivative is
about .0109040lux/m at order16, but no exact general area-source derivative has
been established here.

The finite sun is centred on the oblique incoming direction `(-1,0,3)/sqrt(10)`,
with angular radius .03rad. At the nominal receiver it has area-averaged
visibility .665832979 and illuminance .632455532lux for unit perpendicular
illuminance. Changing angular radius by±.001rad gives FD contributions near0,
-.0041166 and-.0031442lux/rad at orders4/8/16, while a much smaller step1e-4
gives almost zero at all three orders. This is an emitter-boundary aliasing warning,
**not a verified zero physical extent derivative**. A future accurate angular
derivative requires source-domain boundary treatment or stronger convergence.

The near-wall smooth spot-orientation derivative is-.1837614lux/rad at1e-4;
its visibility derivative is correctly zero because orientation does not move
the hard caster shadow of a point spotlight. Cone intensity weighting changes.
Rectangle support widths for h=.5/1/2/3m are .085714/.2/.6/1.8m; perpendicular
sun-cone supports are .030009/.060018/.120036/.180054m. These support formulas
assume parallel source/receiver with the stated edge orientation. The oblique
sun benchmark has different projected width; do not compare it directly to the
perpendicular support formula as a measured RGB penumbra.

### Geometry/optical intervals and localization

Unknown masks/transmission/moving/unsupported wall fixtures return visibility
[2/3,1] in the basic edge case; opaque returns[2/3,2/3]. Missing unbounded geometry
returns[0,1], while a known fully blocking wall plus that omission still returns
[0,0]. A .0001m doorway in a sufficiently tall wall has exact positive aperture
visibility .0005; shallow point-query adaptive sampling reports0 with an
uncertified local error estimate. It is a regression against pretending that
sample equality certifies a closed opening. Patch subdivision2/4/8 reports
shrinking geometric envelopes but keeps true curved visibility uncertain.
Low sources crossing the caster plane are conservatively unavailable to the
projection reference even when independent rays certify selected points blocked.

Local height/intensity fitting results, source truth H4m/I8cd:

| Method | Position errors from starts2.2/3/5.5/7.5m | Strict held-out lux RMSE range |
|---|---|---:|
| Intensity only | 1.800/1.000/1.500/3.500m | .265..316 |
| Intensity + centre visibility | 1.800/1.000/.0154/2.419m | .0106..316 |
| Intensity + integrated visibility | 1.800/1.000/.0000681/2.418m | .0000711..316 |
| Intensity + independent matched edge | <4e-9m at all four starts | about .0000314 |

The perfect supplied edge and caster explain the last row's unrealistic precision;
its measurement band is .005m and would imply a finite height interval, not
nanometre physical certainty. Quantized photometry still gives a small residual.
Integrated visibility succeeded from5.5m in20 local iterations, but not from
fully shadowed starts2.2/3 or the far7.5 basin within80 iterations. The explicit
edge case converged in11..29 iterations. There is no source-layout/general
nonconvex optimization guarantee and no stock recovery error claim.

### Per-sample uncertainty model and recommendation

For a pinned supported compiler recipe, use its declared sampling support,
trace geometry, epsilon and encoding; this room no longer needs the old
35-texel exclusion for its scoped replay. Keep the original historic witness
unchanged and version the additional replay, rather than rewriting old successes.
For unknown recipes, do not universally ignore2WU around every edge. Instead:

1. Retain raw sample, UV footprint, true position/normal, style and provenance.
2. Propagate known coordinate/patch/caster/bias envelopes through the local
   geometry. Trace barycentric slack induces a triangle-size-dependent envelope;
   shadow projection can greatly amplify it near tangent/source-plane events.
3. Treat unknown optical hits as lower/upper contributions. Add cubature,
   source-order discrepancy, mapping and encoding intervals separately. Nested
   differences are estimates, not probability/confidence or certified bounds.
4. For this fixed two-sun point-sample model, unresolved sun2 visibility spans
   red120..148 after encoding; it is not an arbitrary fitted blur. With unknown
   filtering/transport/source parameters, that simple range is no longer a
   general bound. `UnknownProfile` can require `UnspecifiedModel` instead.
5. Use matched shadow geometry for initialization/association proposals, then
   evaluate strict profile-aware RGB on independent receivers/occluders. Keep
   alternative correspondences and low-information cases unresolved.

CPU cost and memory: final numerical benchmark took108.304s and107.547s in
concurrent repeated runs, peak process working sets22,265,856 and22,228,992 bytes
measured through Windows PSAPI. Earlier dual runs took64..65s; elapsed performance
is load-sensitive and excluded from deterministic equality. Final
timing/RSS files remain under `build/x04/verified*/timing.json`; they are separate
from deterministic numerical witnesses. Work budgets in tests range200,000 to
2,000,000 counted operations/evaluation; 16-source-order means256 rectangle nodes
or1,024 angular-cone nodes. This is a high-accuracy reference, not a plan to run
that work on every stock luxel. Sparse accurate boundaries and fast certified
clear/blocked regions should share one request/error contract.

### Exact executed checks

* New `unittest discover ... shadow_footprint`:27 passed, **including** original
  frozen BSP boundary test; no skip in this environment.
* Existing shadow-localization tests:33 passed; bake kernels7; X03 hypothesis/
  isolation tests11 passed. Their original algorithms/fixtures are unchanged.
* Fresh `compare_bakes.py --output build/x04/compiler-all22`:22 passed,66 actual
  BSP/VIS/LIGHT compiler invocations. Original boundary exclusion remains visible
  in that historical harness; the new replay separately verifies all35.
* `compile_variants.py --output build/x04/variants`:7 recipe bakes,21 compiler
  invocations, all completed. Different outputs are measured, not counted as
  failures against the old sampling formula.
* `compiler_boundary.py --output build/x04/boundary-accepted`:35/105/28 reproduced,
  all1,225 interior floor samples byte-exact after restricted replay;35 local
  inspected source files match their recorded lock hashes.
* `ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure
  -j 2`:17/17 passed, including production static visibility, surfaces/patches,
  decode, offline dataset and lifecycle tests.

No shared production/CMake/GLSL/renderer code changed, so SP/MP builds were not
repeated for this Python-only task. No gameplay or GPU rendering validation is
claimed. Remaining limitations: historical Raven rules, general native compiler
trace replay, arbitrary curved footprint visibility, emitter-boundary derivatives,
unknown material masks/transmission, real measured shadow correspondences and
full source-count/XYZ fitting. A correct small sampled objective is not a proof
of any of these.

Exploratory checks exposed and corrected fixture/tool errors before the final
witness: the first Python shader test incorrectly used Quake III's sky bit
instead of JA's0x2000; the initial doorway alias test used a wall low enough to
allow over-the-top illumination, so it was replaced with the intended tall-wall
case; a contrast-vs-centre test initially chose a fully blocked footprint; and
Windows RSS instrumentation needed explicit64-bit HANDLE argument types. Two
early benchmark commands failed while writing timings after numerical work,
and are not reported as successful final runs. The final source hashes, tests,
repeat equality and artifacts correspond to the corrected versions. None of
these fixes changes production rendering or the original compiler tolerances.
