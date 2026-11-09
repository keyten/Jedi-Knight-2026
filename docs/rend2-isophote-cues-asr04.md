# ASR04: planar isophotes, gradients and source cues

2026-11-09, parent `c3431f732`, active `rend2-unified-wip`. The local checkout's
origin is `keyten/BetterJK`; the request names `keyten/Jedi-Knight-2026`. Existing
ASR03 workspace edits were preserved. ASR04 adds only Python research tooling,
measured artifacts and documentation.

**Recommend bounded planar projection cues, conditional on their assumptions.
Do not infer physical source height, type or orientation from an unverified
lightmap transfer function.** Three controlled compiled point-light receivers
gave isophote projection errors of 0.03–0.19 WU. Combining their nonparallel
normal lines gave 0.098 WU 3D error. However, gamma and transport counterexamples
pass a small-residual gate while giving seriously wrong physical positions.
These are measured errors on owned cases, not positional confidence bounds or
stock Raven source-recovery results.

## Reviewed evidence and code

Mariyanayagam & Bartoli, *The Shading Isophotes: Model and Methods for Lambertian
Planes and a Point Light*, CVIU 248:104135 (2024):
[author manuscript](https://encov.ip.uca.fr/publications/pubfiles/2024_Mariyanayagam_etal_CVIU_isophote.pdf),
[publisher record](https://doi.org/10.1016/j.cviu.2024.104135).
Read manuscript §§3–4, 6–7, especially proposition 1 and the detection methods.
The model assumes one point source, piecewise planar untextured Lambertian
surfaces, constant unknown albedo per plane, no interreflection, calibrated
perspective geometry, positive decreasing falloff and a strictly increasing
camera response. World isophotes are concentric circles; their images are
conics. The bottom-up detector filters with Wiener/Gaussian filters, thresholds
intensity bands and fits ellipses; it is noise-sensitive. The top-down method
jointly estimates a rectifying homography and a monotonic cubic-spline radial
profile; dense refinement follows. Here the planes are already known, so world
circle fitting needs neither camera reconstruction nor a fitted homography.
This prototype does not reproduce the paper's full reconstruction algorithm.

The repository's [localization research, §4.1](rend2-light-localization-research.md)
already derives point-response gradients and warns about material, normal,
transfer, source-mixture and shadow ambiguities. Current code inspected:

| Code | Relevant existing behavior |
|---|---|
| [`surfaces.h`](../shared/rd-rend2/lightrecon/surfaces.h), [`decode.h`](../shared/rd-rend2/lightrecon/decode.h) | Original luxel/geometry correspondence, geometric versus shading normals, coverage, quantization/censoring and explicit transfer uncertainty. |
| [`hidden_candidates.cpp`](../shared/rd-rend2/lightrecon/hidden_candidates.cpp), `ShapeScore` | Already normalizes contrast separately per material group and rejects insufficient contrast. It evaluates supported forward hypotheses; this task does not replace it. |
| [`prepare_blind.py`](../tools/rend2/datasets/prepare_blind.py), `eligible` | Existing covered-surface, style, area and invalid-geometry admission rules reused by the study's adapter. |
| [`bake_profile.h`](../shared/rd-rend2/lightrecon/bake_profile.h), historical forward evaluator | Profile-specific attenuation, sampling and encoding remain separate from a physical source integral. This study's baseline is a small relative inverse-square fit, not a modification or claimed replay of the historical evaluator. |
| [`visibility.h`](../shared/rd-rend2/lightrecon/visibility.h), source support | Visibility/domain queries can be Unknown. The prototype performs no occupancy/ray queries and supplies no placement or visibility certificate. |

## Derivation on an already known plane

The following is our specialization of the repository's L2–L3 equations, not a
claim to reproduce the paper's closed-form unknown-pose solver. Write a receiver
as `x=o+u e1+v e2`, with fixed unit normal `n`, and a source as
`s=o+a e1+b e2+h n`, `h>0`. Set `q=(a,b)`, `t=(u,v)`, `D=h²+|q-t|²`.
For one unoccluded isotropic inverse-square source and uniform diffuse response:

```text
E(t) = A h D^(-3/2)                    A is an unknown positive gain/amplitude
grad_t log E = 3(q-t)/D
Hessian_t log E = -3 I/D + 6(q-t)(q-t)^T/D²
at t=q: Hessian_t log E = -3 I/h²
```

The gradient is a **tangent-plane vector toward the source's plane projection**,
under these assumptions. It is not the 3D vector `s-x`: the latter has a normal
component `h`. Even this ideal gradient cannot be treated as an incoming-light
ray. Constant positive gain cancels from log derivatives and changes only the
values assigned to isophotes, not their centers.

Each level `E=l` has `|q-t|²=(A h/l)^(2/3)-h²`: a circle with curvature `1/r`.
Given the linear-response inverse-square law, fitting
`l^(-2/3)=c r²+d` gives `h²=d/c`. Without that law, circles locate only `q` and
the normal line `s=foot(q)+h n`. Two nonparallel planes attributed to the same
source can intersect those lines without using intensity ratios. Parallel or
nearly parallel lines leave height unavailable or poorly conditioned.

### What survives the nuisance terms

| Effect | Surviving cue and limitation |
|---|---|
| Unknown uniform positive gain per plane | Circle centers, gradient directions and the correct-law height ratios survive; physical intensity remains unknown. Spatially varying gain does not cancel. |
| Unknown common strictly increasing scalar transfer `Y=T(E)` | Level-set locations and positive gradient directions survive in the continuous single-source model. Height from intensity ratios/log-curvature does not. For arbitrary `h'`, a different monotone transfer can reproduce the same radial field: one-plane height is structurally ambiguous under unrestricted transfer. |
| Gamma `Y=E^gamma` | `grad log Y=gamma grad log E` and `H log Y=gamma H log E`. At the peak, assuming gamma=1 would infer `h/sqrt(gamma)`. Away from the peak, even that correction is insufficient without fitting the transfer. |
| Additive constant background | Preserves radial centers/directions when truly spatially constant; invalidates the zero-background log/intensity height formula. A varying bounce/sky term need not preserve either. |
| RGB8 quantization | Each rounded scalar code has at most half-code rounding uncertainty, not an irradiance bound. A central difference at pitch `delta` can have error `1/(2 delta)`; a second difference can have error `2/delta²`. Ties, short arcs and differentiation destabilize localization. |
| Saturation/peak compression | Clipping is non-invertible and creates plateaus. Remaining contours can sometimes be informative, but this prototype conservatively vetoes patches with saturated training samples. Coupled RGB processing is not assumed to be one known scalar transfer. |
| Changing normals | For `d=s-x`, an additional term `((partial_t n)·d - n·t)/(n·d)` enters `partial_t log E`, besides `3(d·t)/|d|²`. A geometric plane can still have varying shading normals. Mask such changes. |
| Multiple lights | `grad log(sum E_j)=sum (E_j/E) grad log E_j`; the Hessian also includes the weighted covariance of the individual log gradients. It generally points to no individual source. Coaxial mixtures may still have perfectly circular isophotes. |
| Baked shadows | `E=V E_direct` adds visibility derivatives, discontinuities and filtered edge structure. A hard edge can dominate a gradient. Data-only smoothness or circularity does not prove clear geometry. |
| Material/chart seam | Cross-boundary gains and sampling discontinuities can mimic source structure. Same material ID is necessary, not proof of constant historical diffuse response. Do not divide lightmap codes by texture RGB. |

A point source has no recoverable emission orientation. A broad flat spotlight
whose cone contains every admitted receiver produces the same observations as
a point with the same falloff. An observed elongated patch can also be a shadow,
transport component or mixture. Type and axis need independent fixture/target
evidence or discriminating receivers beyond the cone boundary.

## Bounded implementation and input protocol

[`prototype.py`](../tools/rend2/isophotes/prototype.py) implements three methods
on exactly the same training samples:

1. **Gradient:** central differences within admitted training cells; discard
   stencils below twice the code-quantization derivative error; fit the
   intersection of tangent gradient-direction lines. Require 12 stencils and
   smallest direction-normal eigenvalue / trace >=0.01. The median height
   inferred from log-gradient magnitude is an explicitly law-conditioned
   diagnostic, not a physical depth claim.
2. **Isophote geometry:** interpolate training-only adjacent grid crossings at
   four fixed intensity fractions. Fit circles in world plane coordinates;
   retain radius/curvature, residual, angular coverage and separate centers.
   Discard unsupported levels and require three accepted contours. Use their
   center mean as the projection; intensity/radius height is diagnostic only.
3. **Direct forward fit:** fit `gain*h/D^(3/2)` with one nonnegative nuisance gain
   per patch. A fixed 9³ source grid followed by six 3³ refinements is capped at
   891 trials. Projection search extends 128 WU beyond receiver bounds; height
   is 8–512 WU. This is a bounded local search, not a complete global solve.

The [policy](../tools/rend2/isophotes/policy.json) fixes at most 512 receivers,
four levels, 12 crossing points per contour, at least six occupied angular bins
out of twelve, circle relative RMS <=0.04, center spread <=one receiver pitch,
relative contrast >=0.08, and held-out RMS / training range <=0.06. Source-bound
hits are unavailable. Material/normal/saturation metadata vetoes are separate
from the unguarded numerical diagnostics retained to expose failures.

[`run.py`](../tools/rend2/isophotes/run.py) reads existing owned q3map2 JA
captures under `build/x02/compiled-fixtures-frozen`, verifies original BSP
digests, and selects covered style-zero same-material faces with matching
geometric/shading normals. The deliberately small adapter supports axis-aligned
planes and checks planarity to 0.001 WU. It uses original world sample positions
and mean raw RGB **code proxies**, never labels RGB8 as linear irradiance or SI.

Eight original compiled patches cover `point_room` (three nonparallel faces),
`two_colour`, `spot`, `door_occlusion`, zero-bounce `bounce_control`, and actual
one-bounce `bounce_colour`. Twelve explicitly authored relative fields on that
compiled geometry test gain, gamma, quantization, seams, clipping, a shadow
step, normal metadata, a distant source, two points and reflected transport.
They are not claimed to be new compiler bakes.

The 16-WU grids contain 289 or 425 samples. A fixed geometry-only block pattern
holds out 73 or 107 samples; estimation uses 216 or 318. No held-out value enters
derivatives, crossing extraction or fitting. This interleaved numerical check
is spatially correlated through the bake/filter and is not an independent
production benchmark. Original ASR01/ASR02 folds and scores are untouched.
Source recipe values open in the scoring controller after estimates freeze;
the estimator API receives no answers. This is not an OS sandbox or a claim
that the human/agent had never seen the older fixture recipes.

## Measured comparison and adversarial failures

[Full 20-case table](../tools/rend2/isophotes/results/v2/tables.md),
[numerical outputs](../tools/rend2/isophotes/results/v2/results.json),
[pre-scoring estimates](../tools/rend2/isophotes/results/v2/frozen-estimates.json),
[input/code protocol](../tools/rend2/isophotes/results/v2/protocol.json), and
[verification checks](../tools/rend2/isophotes/results/v2/checks.json) preserve
the measurements. Errors below are WU; 3D columns are **conditional model
diagnostics**, including estimates rejected by gates. `C` means conditional
feature availability; `U` means unavailable, not a physical-source verdict.

| Case | Gradient projection / 3D | Isophote projection / 3D | Forward projection / 3D | G/I/F |
|---|---:|---:|---:|---|
| Compiled point, floor | 2.45 / 3.32 | 0.19 / 2.27 | 0.25 / 2.70 | C/C/C |
| Compiled two colours, floor | 89.41 / 95.53 | unavailable | 72.56 / 124.35 | U/U/U |
| Compiled spotlight, floor | 83.29 / 91.15 | unavailable | 123.85 / 162.61 | U/U/U |
| Compiled doorway shadow, floor | 2.36 / 2.99 | unavailable | 11.14 / 36.89 | U/U/U |
| Compiled one-bounce, floor | 7.44 / 37.24 | unavailable | 10.30 / 35.57 | C/U/C |
| Authored ideal field | 0.15 / 0.88 | 0.01 / 0.26 | 0.25 / 0.48 | C/C/C |
| Same field, square-root gamma | 0.08 / 101.35 | 0.01 / 96.05 | 1.50 / 102.96 | C/C/C |
| Material gain seam | 6.05 / 6.05 | unavailable | 24.77 / 42.76 | U/U/U |
| Saturated field | 6.87 / 8.64 | unavailable | 19.83 / 352.56 | U/U/U |
| Hard shadow step | 0.10 / 1.15 | unavailable | 32.27 / 61.69 | U/U/U |
| Isolated mirror-return component | 25.42 / 117.92 | 25.55 / 117.37 | 25.78 / 116.76 | C/C/C |

On the three compiled single-point planes, projection errors span
0.25–2.45 WU for gradients, 0.03–0.19 for isophotes and 0–0.25 for the direct
fit. Combining the three isophote normal lines yields `(-96.028,32.084,179.958)`,
0.098 WU from the known point. Combining the two gamma-control planes yields
0.020 WU error even though their single-plane heights are wrong by roughly
89–101 WU. These controlled results favor geometry over an assumed code law;
they do not establish stock precision at sub-luxel scales.

Multiplying the ideal field by 17 leaves all three estimates unchanged within
numerical tolerance. Rounding that field to codes gives isophote projection /
3D errors of 0.03 / 0.24 WU. Changing-normal metadata and all requested seam,
saturation and hard-edge controls are gated as intended. The distant-height
control is unavailable rather than certified at the finite search boundary.

Two failures survive the photometric checks. The one-bounce forward fit has
35.57 WU position error at only **0.0193** normalized held-out RMS. The gamma
control passes with RMS 0.023–0.027 despite roughly 100 WU height bias. The
mirror counterexample supplies the isolated reflected component of a point at
`(-86.857,4.571,45.714)` across `.2x+.1y+z=88.8`; its virtual point is
`(-64,16,160)`. The receiver and real point are on the same side of that mirror;
the direct component is suppressed. This is an analytical specular-return
ambiguity, not a diffuse compiler-bounce replay. Identical planar shading cannot
identify which point is the primary lamp.

## Availability rules and proposed feature carrier

| Status | Rule |
|---|---|
| **Usable for a projection proposal under an attested model** | Known planar support/frame, uniform material/normal response, resolved style/chart, adequate contrast and multi-level angular support, consistent centers, no contaminated/censored samples, and independently supported single-source/direct-light assumptions. Output the projection-normal line with measured support/error diagnostics. |
| **Conditional** | Geometric/model checks pass but transfer, direct-versus-bounce attribution, source count or ray/domain support is unresolved. This is the strongest positive status emitted by the study. Preserve point/transport, point/spot and profile alternatives. |
| **Unavailable** | Nonplanarity/normal change, mixed materials/chart support, clipping, rank/contrast failure, short/noncircular arcs, inconsistent level centers, large validation discrepancy, or search-bound hits. Split or acquire better support; do not fabricate a position. |

Physical depth from one plane additionally requires a validated attenuation and
transfer/background model. Multi-plane geometric intersection removes the
single-plane transfer/depth dependency only if the same source attribution is
supported and the normal-line system is well conditioned. Neither case resolves
primary versus transport automatically. A low RMS is necessary for the tested
model, not sufficient for existence, type, orientation or historical identity.

Recommended bounded research feature record: original map/surface/material/
chart/style and sample IDs; plane frame and covered-world footprint; at most
four contour levels with center/radius/curvature, point count, angular coverage
and residual; gradient stencil count and 2×2 conditioning; projected foot and
normal-line family; center dispersion, transfer/profile alternatives and named
unavailability reasons. Keep height as an optional profile-conditioned field,
not a canonical emitter. Existing receiver/evidence/provider interfaces can
carry supporting references; no new canonical storage architecture is proposed.
Later integration must independently apply existing source-support, domain,
visibility and fitting checks. Unknown geometry must stay Unknown.

Acceptance should retain the seven automated mathematical/adversarial tests,
the actual one-bounce failure, the gamma false-precision test, a parallel-plane
rank test and the flat-spot/point orientation ambiguity. Add footprint-separated
validation, chart-boundary handling and perturbation intervals before production
use. Run more maps only after those gates; this small study is not a completeness
or source-count benchmark.

## Reproduction and scope

Python 3.8+ standard library only:

```powershell
python -X utf8 -m unittest tools.rend2.isophotes.test_isophotes -v
python -X utf8 tools/rend2/isophotes/run.py --out build/asr04-new
python -X utf8 tools/rend2/isophotes/run.py --out build/asr04-new-repeat
python -X utf8 -m tools.rend2.isophotes.verify build/asr04-new --repeat build/asr04-new-repeat
```

Use fresh output directories. `--fixtures`, `--bsp`, `--answers` select preserved
owned captures/original BSPs/recipes. Missing or mismatched inputs fail; they do
not produce successful empty results. Regeneration starts with the existing
[q3map2 fixture workflow](../tools/rend2/bake_model/README.md) and
[compiled dataset preparation](../tools/rend2/datasets/test_quality_pipeline.py).
No commercial game assets are needed for these cases. Asset-free unit tests
remain runnable without the old build captures.

Final execution passed **7 unit tests and 126 measured verification checks**.
Two separate study processes produced byte-identical protocol, estimates,
results and tables. Forward work was 837–891 trials per case. Artifact/source/
primary-PDF hashes and execution commands are pinned in the
[manifest](../tools/rend2/isophotes/results/v2/manifest.json). Floating-point last
bits are not guaranteed portable across Python/platform implementations.

The exploratory v1 incorrectly vetoed an otherwise valid patch when one of four
contours had insufficient angular coverage. v2 discards individually unsupported
contours while retaining the original thresholds and requiring three passing
ones. It also includes the actual `bounce_colour` case alongside its zero-bounce
control. The failed v1 output/protocol hashes remain in the manifest; these
revisions were not a blind pre-registered integration test.

No production C++, canonical asset/codec, historical solver, runtime consumer,
world shading or double-lighting behavior was changed. No sources are certified.
