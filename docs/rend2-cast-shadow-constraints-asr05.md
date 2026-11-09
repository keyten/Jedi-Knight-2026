# ASR05: cast-shadow constraints for rejection and localization

2026-11-09; inspected baseline `8f8875008`, active `rend2-unified-wip`.
The requested repository is `keyten/Jedi-Knight-2026`; this checkout's origin
is `keyten/BetterJK`. Work uses the supplied checkout without changing remotes.
Existing ASR03/ASR04 work is preserved.

**Use shadows to test conditional source/caster explanations, retaining an
unassigned explanation. Do not promote contrast to a verified cast shadow.**
In the owned beam/wall fixture, two supplied endpoint correspondences reject
8,923 of 8,924 shading-compatible coarse candidates. A finer local grid retains
52 positions, illustrating the remaining measurement uncertainty. A constructed
emission-mask counterexample produces identical bytes without a caster.

## Paper-specific conclusions and missing measurements

The actual 2020 IJCV article was reviewed, rather than substituting its 2018
conference predecessor. Sections below identify paper results separately from
our fixture and proposed policy.

| Paper and sections reviewed | Conclusion and applicability |
|---|---|
| Sato, Sato & Ikeuchi, *Illumination from Shadows*, TPAMI 25(3), 290–300 (2003), [author PDF](https://research.nii.ac.jp/~imarik/resources/papers/TPAMI-IlluminationFromShadows.pdf), §§I–V, Eqs. 2–12 | Known caster/receiver geometry gives visibility-weighted angular response columns and a nonnegative illumination solve. Illumination is distant: finite lamp distance is absent. The camera response is linearized; physical scale needs calibration. Known reflectance uses one shadow image. Unknown textured Lambertian reflectance cancellation requires an additional image without the object; uniform unknown reflectance can instead be fitted, including their simplified specular model. Strong caster/receiver interreflection is excluded. Adaptive angular sampling helps representation, not finite lamp localization. |
| Santo et al., *Light Structure from Pin Motion: Geometric Point Light Source Calibration*, IJCV 128, 1889–1912 (2020), [publisher full text](https://link.springer.com/article/10.1007/s11263-020-01312-3), §§3–5, Eqs. 1–4, 17 | Caster–shadow collinearity supports near-position or distant-direction calibration. Moving planar targets, estimated target poses and matched shadows allow joint caster/light estimation; caster positions need not be measured beforehand. Geometric fitting does not require calibrated material reflectance or an object-off image. Multiple simultaneous lights require additional correspondence tracking. Neither target motion nor those tracks exist in an ordinary static BSP bake. |

The following dependencies are **unavailable by default** in ordinary JA BSPs:

| Dependency | Sato | Santo | JA consequence |
|---|---|---|---|
| Object present/absent pair | Required for textured-reflectance ratio route, not every variant | Not required | One bake cannot supply the missing counterfactual. |
| Movable target / calibrated poses | Not required | Required by calibration procedure | Static geometry cannot supply a motion sequence. |
| Known edge/point correspondence | Known geometry supplies visibility, rather than manually matched edges | Matched shadow observations are essential | Mesh IDs do not identify which edge cast a baked transition. |
| Calibrated material / response | Known or constrained fitted reflectance; linear response | No photometric material calibration for geometry | Same shader ID is a filter, not a material or transfer calibration. |

These conclusions motivate conditional diagnostics, not a reproduction of either
paper's optimizer or a historical JA light estimator.
The [source manifest](../tools/rend2/cast_shadows/sources.json) pins reviewed
versions, sections and inspected code hashes.

## Current code and integration boundary

Inspected `lightrecon/shadow_evidence.{h,cpp}`, `visibility.h`, `surfaces.h`,
`tr_lightrecon_types.h`, [S09](rend2-shadow-evidence-s09.md), the existing
[localization research](rend2-light-localization-research.md), and its Python
fixture. S09 already has bounded same-page/style gradient extraction, explicit
alternative explanations, finite caster association, separate oracle bindings,
source-dependent retracing and a null association. It also records an admitted
adversarial non-shadow. ASR05 supplies a focused additional research witness and
an operational interpretation of its outcomes; it does not replace that solver.

Canonical surface/luxel identities, original positions, asset/storage schemas,
historical fitting, conditional reconstructed-light storage and runtime rendering
are unchanged. The new JSON schema is a research report, not an asset contract.
There is no runtime consumer, world-shading change or double-lighting treatment.

## Mathematical constraints and conventions

Use right-handed JA coordinates, +Z up. Convert world units to metres once via
`worldUnitsPerMeter`. Receiver-to-source direction is `normalize(p-x)`;
canonical emitter-frame forward denotes photon propagation in the opposite
direction. All positions below are metres, directions and visibility are
dimensionless, angular tolerances are radians.

For Sato's angular model, the calibrated outgoing radiance at receiver `j` is

\[
 y_j=\sum_k f_{jk}L_k V_{jk}(n_j\cdot\omega_k)_+\,\Delta\Omega_k,
 \qquad L_k\ge0. \tag{C0}
\]

`L_k` is incident radiance in W/(m² sr), `f` the BRDF in 1/sr, and
`DeltaOmega` solid angle in sr. Visibility changes the response matrix;
unmodeled material/transfer changes can also change it. Raw JA bytes do not
provide calibrated `y_j`. This angular formulation has no finite-source range.

For a supplied point caster `c` and its actual receiver shadow `s`, a finite
source must satisfy

\[
 (c-s)\times(p-s)=0,\qquad p=s+\lambda(c-s),\quad\lambda>1. \tag{C1}
\]

The cross product has units m². The perpendicular line residual
`||(I-ddᵀ)(p-s)||`, `d=normalize(c-s)`, is in metres. One correspondence leaves
a ray, not a position. Two nonparallel, correct point correspondences can
intersect; parallel or repeated lines remain underdetermined. Inconsistent
lines need residual checks, not a forced intersection. The reused tiny linear
solver returns unavailable at numerical singularity; it is not a statistical
conditioning certificate.

For an associated finite mesh edge `[a,b]`, only the plane constraint follows
from a shadow point:

\[
 N=\operatorname{normalize}((b-a)\times(s-a)),\qquad
 N^T(p-a)=0. \tag{C2}
\]

One straight shadow edge gives one plane; repeatedly sampling it does not give
three independent constraints. Three independent planes may intersect at a
point, subject to ordering, actual finite support, silhouette and ray checks.
Matching the projected **endpoints** provides additional information that an
unlabeled straight-edge segment does not. This distinction is explicit in the
fixture and its degeneracy tests.

For our wall `y=0`, caster depth `c_y>0` and `p_y>c_y`, projection is

\[
 q(p,c)=p+\frac{p_y}{p_y-c_y}(c-p). \tag{C3}
\]

Admit `||q(p,c)-s|| <= b` with a declared **receiver-space** bound `b` in metres.
Do not reuse `b` as a source-position error bar or a plane-residual tolerance:
projection magnification and caster uncertainty change its propagation. Finite
edge testing compares the receiver observation to the nearest point of the
projected finite segment and checks blocked/visible sides and hit identity.

For an isolated physical isotropic point with calibrated data,
`E(x)=I max(0,n·(p-x))/||p-x||³`: `I` is candela, `E` lux, distances metres.
This study instead uses an explicitly relative synthetic amplitude, fill and
linear half-up quantizer `Q(v)=clamp(floor(255v+0.5))`. Its byte values are not
JA RGB8 calibration, and its inverse-square law is not an assumption about
unknown Raven compiler profiles. Unknown transfer, coupled channel compression,
clipping and bounce preclude using that physical formula on arbitrary map bytes.

Pure geometry remains useful without intensity calibration, but **recognizing
which boundary belongs to which caster/source is still required**. In the
distant limit only direction may survive; a finite search box cannot establish
a finite upper distance. Finite emitters, unknown bake filtering and mixed
sources require wider/unknown boundary support, not automatic point fits.

## Reproducible beam/wall experiment

[`study.py`](../tools/rend2/cast_shadows/study.py) defines a finite opaque box
`x=[-0.3,0.3], y=[1.9,2.1], z=[1.2,1.6]` and a receiver wall `y=0`, normal +Y.
The first point source is `(0.2,4,2.8)`, relative amplitude 12, with fill 0.08.
The beam's two bottom silhouette endpoints are `(-0.3,2.1,1.2)` and
`(0.3,2.1,1.2)`. Their matched shadows are supplied as oracle observations.
An independent segment/AABB slab calculation validates actual visibility on
both sides; projection formulas alone do not label a boundary.

The source search is explicitly bounded to 8,959 candidates, spacing 0.1 m,
`x=[-0.8,0.8], y=[3,6], z=[2,3.6]`. Three close, unoccluded training receivers
around `(-1.5,0,1)` have codes `[138,138,138]`. Each candidate fits amplitude
at the centre; other samples allow one byte. No receiver normal, source axis or
source distance is silently fixed during this 3D enumeration.

| Measured experiment | Result |
|---|---|
| Shading-compatible coarse population | 8,924 candidates; maximum truth distance 2.375 m |
| Add one supplied endpoint, receiver bound 0.025 m | 3 candidates; maximum truth distance 1.082 m |
| Add both supplied endpoints | 1 coarse candidate, at the supplied truth |
| Finer local grid, spacing `(0.01,0.02,0.02)` m | **52** compatible positions, `x=[0.18,0.22], y=[3.9,4.12], z=[2.72,2.9]`; maximum truth distance 0.1565 m |
| Shift both measured endpoints by `(0.008,0,-0.006)` m, same bound | 2 coarse candidates; truth retained; maximum truth distance 0.1414 m |
| Fixed photometric tie baseline `(-0.8,3,2)` | Position error 1.625 m; 18/35 held-out visibility mismatches; byte MSE 5176.94 |
| Endpoint-selected candidate, frozen before held-out evaluation | Zero owned-truth position error; 0/35 visibility mismatches; byte MSE 0 |

The baseline is the declared lexicographic tie choice, not an optimization
benchmark. Candidate rejection and admissible-set contraction are the main
results. The fine search is local; neither sampled bounds nor zero coarse-grid
error proves continuous uniqueness or probabilistic coverage. The 0.025 m bound
is supplied to the experiment, not inferred from an unknown JA bake.

A second source `(-0.6,5,3.2)`, amplitude 7, demonstrates two-source geometry
with supplied source labels and two endpoint matches per source. Line
intersections have errors below `2e-13` m in this exact synthetic arithmetic.
At receiver `(-0.5,0,0.1)`, the first source is blocked and the second visible;
the signal is 0.25182, above fill. A shadow need not be black. This is not blind
source-count recovery or automatic mixture separation.

### Counterexample and why ray pairs alone are insufficient

Remove the beam and give the emitter a binary angular mask that suppresses
exactly its former beam-facing ray directions. All **135** strip samples have
the same codes as the true cast-shadow scene. The material, chart, normal and
encoding metadata are identical. This is an explicit constructed emission
distribution, not a claim that a standard JA spotlight or texture albedo
implements that mask. In particular, texture albedo should not be multiplied
into raw lightmap bytes to fake a JA lightmap counterexample.

Contrast detection returns `PossibleContrast` in both scenes. In the scene with
no caster, complete-geometry rays are visible on both sides: the supplied
cast-shadow correspondence is `Incompatible`. Without a supplied correspondence
it is `Unknown`, with no source veto. Even matching geometry cannot establish
causation when cone, indirect or authored-gain explanations remain possible.

For wrong source `(0.8,5.8,3.6)`, all three bright/dark ray pairs still have the
expected visible/blocked pattern. Nevertheless its projected edge misses the
measured boundary by 0.06182 m, exceeding the 0.0145 m strip bound. Finite-edge
reprojection rejects that association. Visibility tests are necessary but do
not by themselves measure shadow-boundary location.

## Bounded diagnostic rule for mapped lightmaps

The Python strip demonstrator exercises the following policy; a full-map
implementation should use the existing S09 components rather than grow a second
solver. All numeric thresholds below are **owned-fixture settings**, requiring
compiler/stock-map validation before adoption as defaults.

1. Preserve original sample identity, mapped position, geometric/shading normal,
   material, chart, page, style, coverage and role. Select spatially adjacent
   mapped luxels, not unrelated neighbouring atlas bytes. Require comparable
   source-domain observations and a stated noise/filter/error budget. Unknown
   transfer or geometry produces `Unknown`; keep raw bytes intact.
2. Exclude chart/material/style seams, normal creases above 0.05 radians,
   saturation/near-clipping, unmapped or mixed geometry, moving casters and
   unsupported optics. Same material ID does not rule out spatial material
   variation, bounce or an authored bake operation. Retain alternatives.
3. Propose contrast only with three coherent adjacent strips, one sharp
   transition per strip, and smooth shoulders. Here the jump threshold is
   `max(8,8*noiseBoundCodes)` with quantization bound 0.5 code/sample; shoulder
   steps must be at most one quarter of the jump. Require same polarity and
   boundary X spread <=0.06 m. Quantization-only variation and smooth ramps
   fail. This restricted prototype supports horizontal scans of a planar wall;
   it does not claim general 2D contour extraction or arbitrary wall rotation.
4. Carry the receiver bound `halfSpacing + mappingError + knownFilterRadius`.
   Unknown filter radius stays unknown. The demonstration uses 0.025 m spacing,
   0.002 m mapping error and zero known filter radius, hence 0.0145 m. If the
   bound reaches twice the spacing, this strip test abstains. Real receivers
   need actual footprint support and compiler boundary sampling uncertainty.
5. For each bounded source/caster hypothesis, check finite projection, ordering,
   edge tangent/support and rays beyond the error band on both sides. Require
   a definite opaque hit on the hypothesized caster on the dark side and a
   known visible bright side. For BSP integration, use structured visibility
   results, reliability, hit IDs and guarded bias; PVS or no hit alone is not a
   clearance certificate. The fixture's no-hit result is clear only because
   its complete synthetic geometry is explicitly known.
6. Return `Compatible` for conditional agreement; `Incompatible` for a failed
   externally supplied correspondence; `Unknown` for missing information,
   failed automatic association or exhausted work; `PossibleContrast` before
   association. Preserve all competing and null explanations. Reject a source
   configuration only under the stated single/isolated component and verified
   correspondence assumptions, never merely because an arbitrary map edge
   cannot be explained. A failed source/caster pair does not reject every lamp.

The prototype caps eight rows, 2,048 samples, one transition per row, 128 rays
per association, eight oracle endpoints and 10,000 candidates per enumeration.
Candidate-limit exhaustion fails explicitly; patch/ray exhaustion is `Unknown`.
These are per-call bounds, not a claim of full-map throughput. A production
extension also needs global budgets, cancellation, memory limits, spatial ROI,
rank/conditioning diagnostics and logged omitted hypotheses, as S09 provides.

Controls exercised include missing geometry, unsupported optics, unknown
transfer/filter, wide filter support, seams, materials, normals, saturation,
quantization, smooth falloff, moving casters, unresolved indirect transitions,
and sample/ray/candidate limits. All automatic results retain
`verified_cast_shadow=false`; heuristic scores are not calibrated probabilities.

## Reproduction and validation

Python 3.8+ standard library only, from repository root:

```powershell
python -m unittest discover -s tools/rend2/cast_shadows -p 'test_*.py' -v
python tools/rend2/cast_shadows/study.py --output build/asr05/results.json
python tools/rend2/cast_shadows/study.py --output build/asr05/repeat.json
Get-FileHash build/asr05/results.json,build/asr05/repeat.json -Algorithm SHA256
python -m unittest discover -s tools/rend2/light_localization_research -p 'test_*.py' -v
python -m unittest discover -s tools/rend2/shadow_fit -p 'test_*.py' -v
```

22 new tests, 33 existing localization tests and 3 S09 witness gates pass:
**58 tests total**. The gates inspect the existing S09 witness; they do not
rerun its C++ fitter. The tracked [report](../tools/rend2/cast_shadows/results.json)
matches a second fresh run byte-for-byte on Python 3.8.5, SHA256
`d68cba607d45163ae45c7730e20aa94c4cffb332dc9e4994fa9dc4dcf5ef615f`.
Numerical last bits may vary on other interpreters/platforms.

No new compiled BSP, stock-map shadow labels, compiler-boundary replay,
statistical confidence calibration, renderer build or GPU/gameplay validation
is claimed. The conclusion is practical conditional rejection/localization in
an owned fixture, with an implementable abstention policy for future diagnostics.
