# Full IDF discovery/refinement: follow-up experiments

2026-11-09, parent revision `8f8875008`, `rend2-unified-wip`.

**The complete cycle makes a material difference on matching linear data.** A
four-light scene that yields one initial peak is recovered as four active sources
after 33 discovery rounds, with held-out relative RMSE about `4.93e-9`. The initial
fit's held-out RMSE is 28.57%. The two-light control is also recovered. The same
implementation does not recover the original positions on the two independent
owned compiler-lightmap cases. It is not ready for stock JA source publication.

[Executable workflow](../tools/rend2/idf_full/README.md),
[fine policy](../tools/rend2/idf_full/policy-v2.json),
[frozen protocol](../tools/rend2/idf_full/results/v3/protocol.json),
[models/iteration histories](../tools/rend2/idf_full/results/v3/models.json),
[post-fit metrics](../tools/rend2/idf_full/results/v3/metrics.json),
[tables](../tools/rend2/idf_full/results/v3/tables.md),
[verification](../tools/rend2/idf_full/results/v3/checks.json), and
[hashes](../tools/rend2/idf_full/results/v3/manifest.json) accompany this report.
The prior [ASR02 proposal-only result](rend2-idf-localization-asr02.md) remains
unchanged. The larger follow-up has a substantially different work budget; its
positive results cannot be attributed to a free improvement at ASR02's 384-node cap.

## Correspondence to the paper

The implementation follows Zhang, Cohen and Curless,
[CVPR 2018, sections 3-5](https://grail.cs.washington.edu/projects/lightlocalization/lightlocalization.pdf):

| Stage | Implemented operation |
|---|---|
| IDF and limiter, sections 3.2-3.3 | Nonnegative direct response, minimum observation/response ratio, active limiting observation |
| 3D voting, section 4.4 | A triangulated spherical stencil; sum limiter-triangle solid angles relative to scene centre, approximating surface area |
| Peak proposal | Local maxima, high threshold lowered until proposals exist, nearby-peak merging |
| Discovery, section 5.1 | Remaining additive light after the current hypotheses; associate existing peaks and introduce new hypotheses with zero intensity |
| Intensity progression | Increase only hypotheses associated with current peaks; explicitly fail if the increment makes required residuals negative |
| Joint fit, section 5.2 | Nonnegative linear intensity solve at fixed XYZ, followed by joint position/intensity Levenberg-Marquardt |
| Position refinement, section 5.3 | For each source remove all other hypotheses' contributions, rebuild votes, and relocate to its corresponding peak |
| Repetition | Continue discovery/refinement until the declared error threshold, an explicit failure, or work limit |

The [supplement](https://grail.cs.washington.edu/projects/lightlocalization/lightlocalization_supplementary.pdf)
motivates retaining geometry and alternative indirect explanations. Its scanning
workflow, arbitrary BRDF/material estimation and original experimental datasets
are not reproduced. This is our implementation of the full core cycle for diffuse
isotropic points, not an execution of the authors' unpublished-to-this-task binary.

SciPy 1.16.3 supplies NNLS and MINPACK LM, rather than the paper's Ceres optimizer.
Logistic coordinates keep XYZ inside the declared room box; log intensities remain
nonnegative. A failed joint fit remains a provisional diagnostic model; the
incremental working state retains nonnegative unexplained observations. The paper
does not specify every rollback/association detail. Here, a proposed relocation
that would overpredict required observations is rejected and recorded. Unknown
required coefficients make a fit unavailable; they are never zero illumination.

The spherical stencil is an icosphere with 42 vertices and 80 triangles. A bounded
7x7x7 room lattice produces 343 candidate centres and 14,749 geometry-only positions
including probes. It is not an exhaustive grid over a game map. The finer experiment
adds two local 3x3x3 refinements around vote peaks, with their actual queries counted.

This implements all core stages omitted by the earlier short prototype. Finite
resolution, room bounds, thresholds and work limits are implementation choices,
not claims to duplicate the paper's published numerical table. In particular, the
normalized 3D MSE threshold `1e-6` is not the authors' differently scaled 2D threshold.

## Inputs, units and blindness

There are two types of experiment.

1. **Matching linear controls.** A known convex box supplies 600 surface point
   observations with diffuse normals. A fixed geometry-tile rule assigns
   360 training, 120 validation and 120 final samples before source parameters
   enter. The author/controller generates two-light and four-light data and a
   prescribed uniform indirect-fill negative control. Intensities are relative
   authoring units. The forward equation intentionally matches the inverse
   equation: this is an ideal algorithm control, not independent real-world evidence.
2. **Independent compiler observations.** `point_room` and `two_colour` reuse the
   actual q3map2-captured BSP observations and immutable folds from ASR02. Each
   uses 64 training rows and 30 final rows. The solver receives sanitized geometry
   and training values, never original light entities or source answers.

All cases use one training-derived common normalization factor, recorded in the
protocol. Matching linear data can legitimately be added/subtracted before
encoding. Compiler values instead use a declared conditional gamma-1,
non-sRGB, unsaturated floor-code interval hypothesis, represented by midpoints.
They are **not calibrated linear irradiance**. The fitting kernel there is a
diffuse inverse-square/geometric-visibility approximation; it does not replace
the verified historical compiler operator, its push/cutoff/trace rules or coupled
encoding. Those residuals describe that conditional model and cannot be treated
as exact physical decomposition of arbitrary JA RGB8.

The native visibility adapter is unchanged from ASR02. It requires known-free
static candidate centres, checks clearance, and preserves unknown paths. Its
results are not certificates for unknown swept-motion extents or all bake states.
The ideal box has independently supplied complete interior geometry.

Fixtures, answers, train/final files, policies, numerical libraries and source
identities freeze before discovery. Geometry-cache preparation uses only receiver
geometry and proposed coordinates. Every model in every variant freezes before
the controller opens source answers or final RGB. No final score selects a model
or changes a fitted parameter. The data/API boundary is not an OS isolation claim.

The coarse and finer protocols are sequential exploratory development on the same
fixed data, with the coarse diagnostics already inspected. They are not independent
unseen-data trials and do not certify a tuned algorithm's stock performance. The
finer policy changes peak resolution and increment size uniformly; it does not
change fixtures, folds, hidden source count, matching tolerances or error gates.

## Experiment 1: what the full cycle adds on linear data

Three variants share each input: IDF proposals only; one initial proposal pass
plus NNLS/LM; and the complete repeated cycle. Final source matching is one-to-one
at 2.5%, 5% and 10% of room width, with 5% shown below.

| Scene / fine variant | Active sources | Matched roots | Unmatched sources | Final relative RMSE |
|---|---:|---:|---:|---:|
| Two lights: raw proposals | 3 | 0/2 | 3 | Not fitted |
| Two lights: initial NNLS/LM | 2 | 2/2 | 0 | `6.77e-13` |
| Two lights: complete cycle | 2 | 2/2 | 0 | `6.77e-13` |
| Four lights: raw proposals | 1 | 0/4 | 1 | Not fitted |
| Four lights: initial NNLS/LM | 1 | 0/4 | 1 | 28.57% |
| Four lights: complete cycle | 4 | 4/4 | 0 | `4.93e-9` |

For two lights, the initial proposals are merely rough seeds; joint nonlinear
fitting supplies the accurate positions. More discovery rounds are unnecessary.
For four lights, the initial field exposes one broad explanation. The complete
run performs 33 discovery rounds, 32 intensity increments, 35 joint-fit attempts
and 35 leave-one-source-out relocations. Additional peaks become useful after
the dominant contribution is partly explained. All four true neighbourhoods,
including the weak source, are recovered; the withheld samples agree as well.

The four-light raw optimized model retains five parameter slots. The fifth has
normalized intensity about `4.29e-10` and is excluded by the predeclared evaluation
rule: active means above 1% of the strongest fitted intensity and above `1e-6`.
The two-light fit likewise has a negligible extra slot. No source truth was used
to remove those slots. Raw models retain them so count regularization is visible.

The preceding [coarse experiment](../tools/rend2/idf_full/results/v2/metrics.json)
used .025 increments and no local peak refinement. Its four-light full cycle
stopped with negative residuals: only 1/4 roots matched, although final relative
RMSE improved from 28.57% to 10.92%. The finer experiment uses .005 increments
and two local refinements, and reaches the successful result above. These two
changes were not ablated separately, so the result does not identify which was
responsible for the gain. It demonstrates meaningful sensitivity to resolution
and incremental progress, rather than universal success from the IDF formula alone.

The uniform-fill control has no local point roots. The complete fine run does
not converge, exhausts its 80-round cap, and retains a false one-source partial
explanation with 36.80% final relative RMSE. It is explicitly failed/out-of-model,
not proof that the prescribed indirect illumination is a lamp. Scene aesthetics
still needs environment/transport alternatives and independent acceptance.

## Experiment 2: original compiler lightmaps

The full cycle was also run against actual owned compiler observations. Matching
here uses the earlier declared 32-WU positional tolerance.

| Compiler case / fine variant | Matched roots | Unmatched active hypotheses | Final relative RMSE | State |
|---|---:|---:|---:|---|
| point_room, one pass + fit | 0/1 | 3 | 79.84% | Partial |
| point_room, full cycle | 0/1 | 2 | 52.25% | No remaining vote peaks |
| two_colour, one pass + fit | 0/2 | 1 | 725.21% | Partial |
| two_colour, full cycle | 0/2 | 5 | 14555.46% | Negative residual failure |

The last percentage is intentionally not clipped: false fitted sources can be
near a surface and extrapolate catastrophically on unseen observations despite
lower training error. Neither configuration is accepted. This is the concrete
reason to retain final spatial tests and not promote a low training residual.

The tests combine an approximate compiler-relative response kernel, sparse
receivers, scalar mean-RGB proposal votes and a simple multi-channel extension
of the incremental intensity progression. They do not isolate a single cause of
failure. They establish that merely completing the paper's iterative cycle does
not make this particular adaptation reliable on encoded compiler observations.
They do not establish that a complete profile-matched implementation is impossible.
No full-cycle stock-map success, original historical lamp recovery or calibrated
SI accuracy is claimed.

## Cost, reproducibility and disposition

The fine experiment runs five scenes, three variants and two identical repeats:
30 completed numerical runs. Measured variant runtimes sum to about 144 seconds
on the shared host, excluding geometry-cache preparation and post-fit evaluation.
All repeats match exactly, including positions, intensities, stage histories and
work counters. The shared cache costs 14,749 geometry-only positions per case;
new local/optimizer queries and coefficient visits are exported separately.
The local-refinement 150k-position limit is soft and does not silently constrain
all optimizer operations. No real-time or renderer-cost claim follows.

Four new numerical tests check the closed 42/80 spherical mesh, NNLS+LM recovery,
negative-residual rejection and unknown-coefficient unavailability. Seven existing
ASR02 tests also pass. Verification confirms that the four-light success exercised
every core stage, that positive scenes were not explained by black predictors,
and that production source identities are unchanged.

The resulting recommendation is more specific than the earlier proposal-only
No-Go: **retain the complete cycle as a working offline method for justified
linear direct-light domains; do not integrate this compiler-data adaptation into
the production source finder yet.** Its successful weak-source recovery is real
within the controlled model. Its original-lightmap failures and indirect-control
false hypotheses remain equally explicit.

Only new offline files are added. Historical solver, source finder, canonical
asset/storage contracts, existing strict gates, world shading and runtime/GPU
consumers are unchanged. The tool ends at diagnostic hypotheses and metrics.
