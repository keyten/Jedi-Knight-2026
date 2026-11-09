# ASR14: lightmap colour patches, gradients and partial isophotes

ASR14 adds a bounded, standalone offline feature extractor in
[`tools/rend2/lightmap_patches`](../tools/rend2/lightmap_patches/README.md).
It reuses the original mapped luxel/receiver export and ASR04's small planar
linear algebra. Its optional source-location check uses ASR13's existing
read-only capture geometry bridge. The historical solver, canonical storage
schemas, renderer and world shading have no implementation changes.

The feature domain is **original encoded RGB8**, explicitly without irradiance
calibration. A coloured positive patch can result from a primary light,
transport, spatial reflectance or overlapping contributions. A radial feature
never certifies an exact historical source.

## Receiver and descriptor contract

`org.openjk.lightmap-patches.v1` is a diagnostic JSON schema, independent of
canonical assets. The report freezes the complete numerical policy, rejected
receiver/component counts and work count. The CLI pins row/source/map hashes,
geometry-only selection and, when used, geometry executable/capture hashes.

Each descriptor contains:

| Field | Meaning |
|---|---|
| `id`, `peak_receiver`, `receiver_ids`, `component_receiver` | Deterministic identities derived from original receiver IDs and chart identity. IDs depend on frozen policy and input, not source answers. |
| `chart` | Original image, page, surface, material, style slot and style ID. |
| `normal_world`, `frame` | Geometric receiver normal and a world-unit tangent frame. Shading normals do not replace geometric normals. |
| `maximum_world`, `maximum_fit_rms_codes` | Trimmed multiscale maximum and robust local concave quadratic refinement when supported; otherwise the measured peak receiver. |
| `centroid_world`, `extent_sigma_world`, `major_axis_world` | Positive-basin weighted centre, covariance radii and sign-ambiguous footprint axis. Near-isotropic orientation is unavailable. |
| `peak_rgb8`, `background_rgb8`, `mean_rgb8`, `excess_colour_direction` | Code-domain colours and positive chromatic direction, with explicit domain label. No linear-irradiance interpretation. |
| `scales_texels`, `coverage`, `mapping_issue_counts`, `uncertainty` | Supporting scales; boundary and censored support; original mapping issues; reflectance, filtering and transport ambiguity. |
| `isophotes` | Four scalar code levels with interpolated world crossing samples, bracketing original receiver IDs, circle residual and angular coverage. Partial arcs are retained; closure is never assumed. |
| `gradients` | Same-chart tangent code gradients, original receiver IDs, local affine discrepancy, conditioning and robust gradient-line residual. |
| `apparent_boundaries` | Strong adjacent code jumps with receiver IDs and world midpoint. Shadow/material/filter interpretation stays unresolved. |
| `constraint` | Conditional source plane-projection foot and normal line, or explicit unavailability. Depth and emitter orientation remain null. Radius is a heuristic geometric tolerance, not confidence or a posterior. |

Smoothing never crosses `(image,page,surface,material,slot,style)` boundaries.
Four-connected atlas neighbours must also have compatible world spacing,
geometric normals and tangent-plane position. Holes and missing geometry are
barriers. Ambiguous/shared/multiple-chart texels, normal seams, unsupported
receivers and unknown normal orientation are rejected. Nonplanar connected
components are reported unsupported instead of being flattened into images.

At graph radii 1, 2 and 4, channel-wise trimmed means locate positive maxima
relative to a per-component background. Nearby multiscale maxima are merged.
Geodesic nearest-maximum basins separate multiple peaks, including equal-colour
peaks. Positive chromatic support is grown within each basin. This partition is
a feature segmentation heuristic; it does not separate physical contributions.

Contours require at least 12 crossings, seven of twelve angular bins and a
relative circle RMS at most .06 for radial use. At least two supported levels,
consistent centres and a well-conditioned robust tangent-gradient intersection
must agree. Saturation, clipped peak neighbourhoods and shading/geometric-normal
disagreement withhold the position constraint. Other supported partial contours
remain available for inspection.

The normal-line construction assumes one dominant radial contribution, locally
constant diffuse receiver response, common monotone response, and no visibility
boundary in the fitted support. It uses level-set geometry; it computes no
height from gradient magnitude, log curvature, code ratios or inverse-square
photometry. Point, spot and panel are not inferred source labels.

Compatible colour/style constraints on different, nonparallel receiver surfaces
yield pairwise least-squares normal-line intersections. Positive receiver
half-spaces, line residual and normal diversity gate them. Parallel floor and
ceiling evidence alone cannot locate a point. Alternatives are retained without
claiming their multiplicity equals the number of lamps.

Geometry reports `ConditionalLocation`, `RejectedGeometry` or `UnknownGeometry`.
Only explicit free-space plus two clear rays allows conditional location.
Missing geometry, boundary queries and possible dynamic occupancy stay unknown.
The existing bridge's surface-origin rays can remain unknown; this study does
not change their bias policy or reinterpret them as clear. Even a clear result
does not establish source existence, type or historical identity.

Default limits are 262144 selected rows, 8192 rows per chart, 256 maxima/patches,
12 million counted graph operations and a two-million-line input scan. These
are algorithm limits, not measured process-RSS guarantees. Exceeding them fails
explicitly. Diagnostic extraction has no background jobs or runtime consumers.

## Frozen validation

[`summary.json`](../tools/rend2/lightmap_patches/results/v1/summary.json) and
[`verification.json`](../tools/rend2/lightmap_patches/results/v1/verification.json)
pin the executed cases and implementation. Two complete runs produced identical
descriptor JSON and aggregate JSON. Compiler answers are opened after all
original-compiler descriptors have been written. Inputs are the existing owned
JA q3map2 BSPs and their original mapped exports; no fresh compiler execution
is claimed by this task.

Eight authored code-field controls cover point-like radial, anisotropic spot,
anisotropic panel, two coloured peaks, flat, ramp, coplanar material seam and
GI-only ambiguity. The five direct-positive footprints are detected one-to-one
with precision/recall 1 at 16 WU tolerance. Flat, ramp and material-seam controls
have zero patch false positives. Footprint-axis errors for the anisotropic
controls are below .3 degrees. These axes belong to receiver footprints;
**emitter-orientation precision is unavailable**.

The identical radial GI-only control yields one radial constraint. If treated
as a primary-lamp cue, this is **one false positive**, intentionally preserved
by the regression checks. No physical emitter is published.

Seven original compiled scenes cover point, directional spot, panel, two-colour
multi-light, door shadow, separating-wall shadow and coloured bounce. Floor
labels are the authored source's normal projection; spot uses the authored
central-axis/floor intersection. The panel label is its ceiling footprint,
because its compiled floor signal is below the fixed contrast threshold.
Matching is one-to-one at 32 WU, or 48 WU for spot.

| Compiled scene | Matched / detections / labelled footprints | Precision / recall |
|---|---:|---:|
| Point room | 1 / 1 / 1 | 1 / 1 |
| Spot | 0 / 2 / 1 | 0 / 0 |
| Panel ceiling | 1 / 1 / 1 | 1 / 1 |
| Two colour | 2 / 2 / 2 | 1 / 1 |
| Door shadow | 1 / 1 / 1 | 1 / 1 |
| Separating-wall shadow | 1 / 1 / 1 | 1 / 1 |
| Coloured bounce | 1 / 1 / 1 | 1 / 1 |

The compiled spot produces two measured floor maxima that miss the frozen
axis-centre label. This is an explicit metric failure (two unmatched detections,
one missed label), not evidence that the source axis equals a baked maximum.
Spot and panel produce no radial source-position constraints. No thresholds or
labels were relaxed after this failure. The point room has four supported plane
projections with maximum error .112 WU; five pair intersections are within
.114 WU of the authored source, all retaining `UnknownGeometry`. Two-colour
projection errors reach 6.63 WU; coloured-bounce errors reach 18.04 WU. These
small, correlated fixture errors do not establish general source accuracy.

The compiled material-boundary coverage comes from the red/white receiver
charts in the coloured-bounce scene; the pure coplanar material-step negative
is an authored control on known receiver geometry, not a separately compiled
seam-only BSP. GI-only compiler evidence is the positive matched code increment
between the existing one-bounce and direct-only compiled bakes. Original
geometry and direct lights agree; both recipes declare gamma=1, compensate=1,
exposure=0. Nineteen censored pairs are excluded. Its two patches produce zero
primary-position constraints. This derived, quantized/filter-dependent increment
is explicitly distinguished from a separate original GI-only capture and is
never labelled calibrated linear irradiance.

The fifteen unit tests also cover saturation, every unsupported-mapping mask,
holes, chart/style separation, equal-colour multi-peak support, hard apparent
boundaries, normal mismatch, oblique planes with large world offsets, input
reordering, unknown/blocked/solid geometry and explicit budget failures.

The mounted stock `mp/ffa3` chart 3628 (`textures/desert/s_floor1`) supplies a
real colour-lit region. Its 3143 original exported rows produce 24 descriptors
and no radial position constraints. Selection was exploratory after examining
chart colour contrast, not a held-out source-accuracy experiment. Source truth,
precision and emitter orientation are unavailable. Only hashes and aggregate
counts are checked into git; licensed rows, geometry, descriptors and PNGs stay
under the private `build/asr14` directory.
