# ASR02: intensity-distance fields for blind source proposals

2026-11-09, parent `a93a84526`, active `rend2-unified-wip` checkout.
**No-Go for integration of this prototype.** Under the frozen 384-spatial-node
cap, geometry-aware IDF finds three of four controlled point roots within 32 WU;
the existing Hidden point catalog finds four. No IDF variant adds a root beyond
Hidden's complete returned proposal pool. The uncertainty variant creates eight
false local-point proposals on a positive sky-only control. This is a negative
result for the tested sparse adaptive proposal policy, not a disproof of the
paper's full localization/refinement algorithm.

[Commands](../tools/rend2/idf/README.md), [final policy](../tools/rend2/idf/policy-v3.json),
[pre-run input/binary/code hashes](../tools/rend2/idf/results/v3/protocol.json),
[all measurements](../tools/rend2/idf/results/v3/results.json),
[precision/recall and cost tables](../tools/rend2/idf/results/v3/tables.md),
[checks](../tools/rend2/idf/results/v3/checks.json), and
[artifact hashes](../tools/rend2/idf/results/v3/manifest.json) accompany this report.
The actual configured remote still names `keyten/BetterJK`; the requested
`keyten/Jedi-Knight-2026` label does not change which checkout was measured.

## Derivation and what was actually reproduced

Zhang, Cohen and Curless, [CVPR 2018, §§3–4, Eqs. 1–8](https://grail.cs.washington.edu/projects/lightlocalization/lightlocalization.pdf),
start with known geometry and BRDF and direct reflected radiance. For an isotropic
point at p, define a nonnegative response coefficient

`S(q,p) = V(x,p) f(x, normalize(p-x), w) max(0,n·(p-x)) / |p-x|^3`.

Then `B(q) = sum_j I_j S(q,p_j)`. At a real source position, for every known
positive coefficient, nonnegative additivity implies

`I_j <= B(q)/S(q,p_j)` and `D(p) = min_{q:S(q,p)>0} B(q)/S(q,p)`.

Zero response supplies no constraint; a completely unobserved position has no
finite cap. The active observation `L(p) = argmin_q B(q)/S(q,p)` is the limiter.
Their Eq. 8 counts distinct limiters around p; proposals come from structure in
that field, not maxima of B or D. Their 3D implementation approximates surface
area of limiter triangles on a spherical stencil. Their later iterative discovery,
intensity solve and nonlinear position refinement are **not implemented here**.

The [supplement, §§2–3](https://grail.cs.washington.edu/projects/lightlocalization/lightlocalization_supplementary.pdf)
discusses occlusion-induced false vote peaks and real-scene reflectance/exposure
assumptions. These motivate retaining visibility states and rejection diagnostics;
they do not establish that a vote is a real lamp. The earlier
[repository localization research, §4](rend2-light-localization-research.md)
already warns against using the bound on peak-normalized RGB8. Main PDF page 3
equations/figures were visually inspected, and §§3–5 plus supplement §§2–3 were
reread. Both PDFs' exact SHA-256 identities are pinned in the protocol.

The following are our consequences for this experiment, not extra claims of the
paper. In a calibrated linear domain, let a measurement lie in `[b_low,b_high]`
and its true response lie in `[s_low,s_high]`. If `s_low>0` and `b_high` is finite,
`I <= b_high/s_low` remains a valid individual-source upper bound. If visibility
is unknown, its coefficient can be zero: `s_low=0`, so that observation supplies
no finite cap. Unknown reflectance approaching zero has the same consequence.
Saturation with no justified upper observation bound also removes the cap.

Known nonnegative indirect light can only loosen an individual-source upper bound
on the **total** linear observation. It destroys the inference that similar active
constraints indicate a direct source; a passive reflecting patch can resemble an
emitter. Subtracting a guessed indirect term can invalidate positivity and the bound.
JA lightmaps do not supply the full outgoing directional surface field assumed
by the paper's direct/indirect argument, so no indirect subtraction is attempted.

For JA encoded lightmaps, write `y = Encode_profile(B)`. Generally
`Encode(sum B_j) != sum Encode(B_j)`. Quantization, coupled peak scaling, clipping,
unknown transfer and the compiler's attenuation/angular/receiver placement rules
prevent substituting `y/S` into the physical inequality. A common positive linear
gain would scale D without changing L, but spatial/material-dependent gains and
nonlinear transfer can move L. Texture RGB is not a verified bake BRDF. No texture
albedo is divided out, and no RGB8 value is called calibrated irradiance or candela.

Our prototype therefore computes a **labelled code-proxy transform** with unit
diffuse response and mean RGB8. Its raw/censored bounds are heuristic values.
The tests separately demonstrate the valid linear additive inequality and a
nonlinear-transfer counterexample where the numerical cap falls below true I.
The rigorous inequality survives only when its calibrated coefficients/domain
are independently established; that condition is not established for these
compiled-code comparisons or stock JA.

## Deterministic search and comparison policy

The new Python prototype lives entirely under `tools/rend2/idf`. A small standalone
C++ helper links the unchanged CPU library and opens only sanitized geometry.
It uses existing `BspRegionDomain`, `StaticVisibility::OverlapBox` and ray queries.
Candidate centres require known free space and a clear 0.5-WU box. A cell's centre
certificate is not a certificate for its entire volume, a player-accessible room,
or unbounded exterior geometry. Unknown geometry is never made empty.
These are conditional static-capture checks. The helper does not certify unknown
swept-motion extents or all runtime/bake states; stock hypotheses acquire no
placement certificate from this experiment and still require the existing strict
source-placement and motion checks before any acceptance.

Final owned search bounds come from the complete sanitized geometry's vertex
bounds, with native occupancy/clearance rejection inside that bounded ROI. Most
rooms are `[-192,-128,0]..[192,128,256]`; the aperture control and sky geometry have
their own recorded bounds. Stock uses ASR01's declared hoth3 ROI. No full-map grid
or source-answer-driven ROI is allocated.

There are 27 initial cells (3 divisions per axis). Each cell evaluates the centre
and six quarter-width axis offsets; those seven positions count against the
384-position allowance. The highest-vote unrefined admissible cell splits into
eight children, tied deterministically by XYZ. Three complete refinements use
357 positions / 51 cell evaluations, leaving 27 unused because a complete next
split costs 56. Sparse limiter diversity approximates Eq. 8, not the paper's
spherical/geodesic estimator. The candidates are the highest observed vote cells,
with vote >=2, separation >=24 WU and cap eight; this is a bounded ranking proxy,
not a converged local-maximum or uncertainty estimator.

Variants:

| Variant | Meaning |
|---|---|
| raw | Point-response ratios using exact codes, ignoring **known blocked** paths as an explicitly nonphysical occlusion ablation; unknown paths and inadmissible stencil points still excluded |
| geometry | Same ratios on known-visible paths, with native free/solid/boundary/unknown and clearance handling |
| censored | Known-visible paths plus `[max(0,y-2), y+3]` code intervals; any saturated RGB channel removes the whole vector's finite cap, respecting possible shared-peak coupling |

For censored votes, every observation whose lower ratio overlaps the minimum
upper ratio is a possible limiter. Each label receives inverse-cardinality weight;
the stencil score sums its maximum weight over samples. This is an experimental
ambiguity discount, not a probability, material uncertainty solution or rigorous
IDF vote. All modes remain code proxies. A robust/weighted vote cannot restore a
physical bound when transfer/BRDF are unknown.

Hidden uses the same rows, ROI and 384-node cap, one conditional inverse-square
profile, current default four-seed refinement and its existing point/spot trials.
Its selected point comparison ranks the native point proposals by training shape
score, applies the same eight-output cap and 24-WU separation. Full native pool
coverage is exported separately so catalog filtering cannot manufacture an IDF
discovery gain. Spotlight spatial matching is also reported separately from type
matching; a point proposal cannot claim recovered spot orientation or cone.

These are identical declared **spatial caps**, not identical wall time or actual
work. Hidden stops at 128 nodes for point/door/spot, 216 for two-colour and also
probes types/amplitudes. IDF spends 357 positions, including stencil queries.
All ray/traversal and response-pair costs are retained; sparse IDF gets no
unreported free dense voting grid. It gains no additional roots even with more
actual spatial samples on these cases.

## Fixtures, partitions and blindness

The positive compiler cases are `point_room`, `two_colour`, `spot` and
`door_occlusion`. Controls are original `skylight`, X05's
`x05_sky_aperture_v1`, and an analytically supplied passive reflection patch on
compiled `point_room` geometry. The latter is **not a compiled bounce replay**:
a fixed 128x64-WU ceiling patch has prescribed outgoing radiance 400 in the
control domain, reflectance .5 and externally supplied illumination. Sixteen
quadrature elements generate its receiver response. The known ceiling's launch
is offset 1 WU for visibility only. No local direct point roots exist in this
conditional control; no fitted residual or self-emitting wall is introduced.

| Case | Full train / validation / final counts | Selected training / positive / saturated |
|---|---|---|
| point_room | 117 / 57 / 30 | 64 / 62 / 0 |
| two_colour | 117 / 57 / 30 | 64 / 64 / 0 |
| door_occlusion | 117 / 57 / 30 | 64 / 39 / 0 |
| spot | 48 / 24 / 30 | 48 / 27 / 0 |
| skylight | 78 / 39 / 30 | 64 / 3 / 0 |
| sky_aperture | 123 / 87 / 96 | 64 / 18 / 0 |
| reflected_patch | geometry inherited from point_room | 64 / 29 / 0 |
| hoth3 ROI | ASR01 captured-view folds unchanged | 64 / 49 / 6 |

Positive means a training channel >=8 codes, a signal diagnostic, not direct-light
attribution. New owned folds use the existing splitter with one fixed seed
20261109, block 128, gap 1 plus three footprint radii, origin (17,31,47). Geometry
and stable IDs select up to 64 rows; the spot has only 48 available. The X05
aperture uses its already frozen 20261028 folds and is a partial planar-floor
observation subset, not a complete all-pages capture. Stock folds remain original.

The controller reads original capture records to author new files using only
geometry for assignment. It does not retry seeds based on brightness, source
answers or final error. The older folds are preserved and these sequential
experiments are not statistically independent trials. Before numerical search,
freeze the resulting geometry, rows, receiver files, hidden requests, manifests,
answer hashes, sealed validation/final hashes, source code and executable bytes.
TrainingView values alone reach either proposal algorithm. Answer files first
open after all case/method proposals freeze. Neither search opens validation or
held-out target files, and no held-out score reorders or refines candidates.

The initial [v1 attempt](../tools/rend2/idf/results/v1/results.json) accidentally
restricted search to the training receiver AABB; it excluded parts of the true
room and left a completely dark, one-wall spotlight fold. That is an adapter
failure, not evidence against localization. v2 corrected the source volume and
declared the new geometry-only folds before rerunning. Its raw ablation also
failed to exclude unknown rays. [v2 measurements](../tools/rend2/idf/results/v2/results.json)
remain failed historical evidence. v3 excludes unknown rays/inadmissible stencil
positions in every mode, preserves the v2 folds, volumes and gates, and is the
only integration comparison. The unperformed initial 64-row spot freeze stopped
at 48 available rows; the final policy uses the same available rows for all methods.

## Measured localization, misleading cases and cost

Position-only one-to-one matching uses frozen 16/32/64-WU tolerances. The table
below shows TP/FP/FN at 32 WU. FP here means an unmatched **proposal** in an owned
controlled scene, not a published false physical emitter. Empty denominators
remain N/A. No accepted sources exist and no historical certification is issued.

| Case | Raw IDF | Geometry IDF | Censored IDF | Hidden selected points |
|---|---|---|---|---|
| point_room | 1/7/0 | 1/7/0 | 1/7/0 | 1/1/0 |
| two_colour | 1/7/1 | 1/7/1 | 0/8/2 | 2/4/0 |
| door_occlusion | 0/0/1 | 1/7/0 | 1/7/0 | 1/1/0 |
| spot, position only | 0/0/1 | 0/8/1 | 0/0/1 | 0/1/1 |
| reflected patch | 0/0/0 | 0/0/0 | 0/0/0 | 0/2/0 |
| original skylight | 0/0/0 | 0/8/0 | 0/0/0 | 0/4/0 |
| positive sky aperture | 0/0/0 | 0/0/0 | 0/8/0 | 0/0/0 |

On the four point roots, raw/geometry/censored recall is 2/4, 3/4, 2/4;
proposal precision is 2/16, 3/24, 2/24. Hidden selected points give 4/4 recall
and 4/10 precision. The nearest successful geometry-IDF proposals are 16.055 WU
from the point/door lamp; Hidden is 14.967 WU. The second two-colour source is
about 69.946 WU from geometry/censored IDF, versus 16.492 WU for Hidden.
The full native pool also places a typed spot 8.944 WU from the spotlight;
selected point-only metrics cannot take credit for that. Geometry-IDF's nearest
spot position is about 169.936 WU away; censored/raw return none.

Visibility-aware votes help the occluded scene compared with the raw ablation,
but do not add a source absent from Hidden. The censored vote loses both
two-colour roots at 32 WU and returns eight point hypotheses on a sky-only
aperture. Its limiter ambiguity weighting can create a broad geometric vote
pattern even without a local lamp. Geometry-IDF similarly proposes eight lamps
on the sparsely positive original sky fixture. These are explicit misleading
outcomes; locating a bright region does not count as useful source discovery.
The passive patch is rejected by all final IDF variants on this fold, but the
failed old fold produced eight proposals. That sampling dependence does not
establish general reflected-light discrimination. Hidden's own control proposals
also remain ambiguous hypotheses rather than accepted sources.

IDF uses 22848 geometric rays for 64-row unoccluded rooms, 20608 for the door,
17136 for the 48-row spot and 8384 for geometry/censored hoth3. Native Hidden uses
24576/41472/24576/18432 rays for point/two-colour/door/spot. Exact traversal costs
and all actual node counts are in the generated cost table. Geometry-IDF rejects
35/357 tested positions as solid on the doorway scene. hoth3 additionally has
unknown/boundary/solid positions and thousands of unknown receiver relations;
these remain exclusions, never free space or black observations. All searches
are incomplete, including the unused spatial cap and unsearched exterior support.

Stock hoth3 produces eight IDF hypotheses in each mode and four selected native
point hypotheses. Source truth, precision/recall, accepted-source wall leaks,
photometric fit, and complete GI attribution are **Unavailable**. No inference
of stock accuracy follows from these counts.

## Recommendation and verification

The frozen integration screen requires >=80% recall on each mandatory point
fixture, at least one additional source beyond Hidden, zero local-point proposals
on controls and deterministic repetition. Every IDF variant fails: added sources
are zero, two-colour or occluded recall is insufficient, and geometry/censored
controls produce false proposals. **Do not integrate this prototype into Hidden,
Joint or Producer.** Retain it as an executable negative research result.

A later investigation could test a justified linear/compiler-response domain,
more independent receiver orientations, and the paper's richer 3D limiter-area
estimator before spending a new frozen budget. Such work is not implemented here.
Neither global exhaustive grids nor RGB8 residual subtraction are justified by
this experiment. Any future proposals would still need existing geometry,
full supported forward evaluation, source/transport alternatives and independent
acceptance; no IDF score is a physical or historical certificate.

All final IDF/Hidden semantic repeats match. Seven mathematical/admission tests
pass, including positive additivity, nonlinear-transfer failure, saturation,
unknown geometry and limiter-diversity versus brightness. The unchanged native
Hidden regression passes 1992 checks. Frozen input/artifact hashes, source bytes,
native query caps and known-free candidate centres verify. Production source
identities match ASR01. No source finder, renderer/world shading, runtime consumer,
canonical asset/storage schema or historical strict gate is changed.
