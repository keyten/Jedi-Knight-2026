# ASR24 — original stock source configuration

**The required stock demonstration passes, conditionally.** One fixture-associated
point configuration on original `mp/duel10` is nonempty, geometrically admitted,
supported by original visible-fixture and lightgrid evidence, selected on
validation, and useful on frozen held-out lightmap receivers. It is an aesthetic
**Candidate**, not an exact historical lamp or a canonical resolved asset.

The winning fixture is original BSP surface **942**, material
`textures/mp/s_flatwhite`, world model **0**, catalogue ID
`0138fe7f53ee32b7c6216f22fb9495418754badcf9380b06ba7f9bb1eebe7c0a`.
The fitted receiver is original surface **155**, material
`textures/factory/doorpattern`. Original shader/image hashes and the luminous
footprint identify the fixture independently of lightmap fitting. The native
source-support provider verifies named contact, the emitting side and world-solid
exclusion. **93 distinct original grid records** agree within the declared
30-degree gate and have known-static clear segments; all 93 are also independent
of captured mover uncertainty. These are aggregate direction cues, not 93
independent historical-source answers or statistical confidence votes.

| Frozen winning region | No-local-light baseline | Fitted source |
|---|---:|---:|
| Training interval MSE | 812.311731 | 282.443055 |
| Validation interval MSE | 521.689833 | 197.962933 |
| Held-out interval MSE | 174.230938 | **95.075591** |
| Held-out robust loss | 36.172113 | **24.154715** |
| Held-out maximum residual, codes | 31 | 24.749123 |

Held-out MSE falls **45.43%** and robust loss falls **33.22%**. The folds contain
20/20/17 original receivers in 3/2/1 disjoint spatial blocks, respectively. A
single held-out block limits generalization; these are not whole-map readiness
measurements. Both native fitting runs produce identical reports.

The selected point's relative RGB parameters are approximately
`[18653.738, 20894.557, 18735.768]`, with fixed gauge 1. The shared nuisance RGB is
approximately `[15.986, 0, 0.000017]`. Neither power nor nuisance hits its upper
bound. These are **arbitrary relative parameters**, not watts, candela, nits or
lux. The point position is the original luminous component centroid plus a fixed
4 WU outward stand-off. No position is optimized after observing fit scores.
Actual XYZ and ROI bounds remain in the private geometric report and hypothesis.

Source removal is substantive: with the **same frozen background**, removing the
source raises held-out interval MSE from approximately **95.076 to 7837.109**.
All **17/17** held-out receivers change by at least one RGB8 code; the maximum
change is approximately **114.857 codes**. The independent audit replays geometry
through `Shape::Prepare` and encoding through native `Bake::Encode`, and agrees
with the original native score. It never refits a parameter. This is distinct
from the separately refitted no-local-light baseline in the table.

## Inspect the configuration

Licensed geometry, RGB8, images and coordinate-level outputs remain under ignored
`build/`, following the existing stock debug conventions:

- [Human geometric report](../build/asr24-proof-v1/duel10-grid-2/report.html)
- [Three geometric projections](../build/asr24-proof-v1/duel10-grid-2/geometry.png)
- [Conditional source configuration](../build/asr24-proof-v1/duel10-grid-2/hypothesis.json)
- [Original geometry, fixture polygons, source and grid OBJ](../build/asr24-proof-v1/duel10-grid-2/scene.obj)
- [Every original grid record and checked ray](../build/asr24-proof-v1/duel10-grid-2/geometry.json)
- [Original mapped fitting receiver support](../build/asr24-proof-v1/duel10-grid-2/folds.json)
- [Frozen source-removal check](../build/asr24-proof-v1/duel10-grid-2/ablation.json)
- [Original receiver patch and frozen predictions](../build/asr24-proof-v1/duel10-grid-2/receiver-patch.png)
- [Per-receiver original provenance and source response](../build/asr24-proof-v1/duel10-grid-2/receiver-support.json)

The plate uses equal world scale in each projection. It shows original static
geometry, the luminous footprint, fixed source, original receiver RGB8, held-out
membership, original grid arrows, and the known opaque receiver hit. The fitted
receiver patch is surface 155, linked by that geometric hit and the numerical
source response. ASR14 finds six training descriptors elsewhere in this ROI but
no qualifying local-peak descriptor on the fitted floor chart. ASR15 selects no
link under its conservative surface-endpoint rules. Those unavailable/unselected
assignments are retained; the independent fixture/grid/admission/fit path supplies
the positive evidence. A local-peak detector is not silently substituted for a
source-to-fixture certificate.

## Selection, fitting and provenance

The controller is [documented here](../tools/rend2/stock_aesthetic/README.md).
It calls the existing ASR09 catalogue, ASR14 patch extraction, ASR15 associations,
ASR12/13 grid bridge, ASR07 solar audit, ASR20 fitting and, for a recorded failed
study, ASR21 sparse selection. No alternative ray tracer or fitter is introduced.

The final source hypotheses are the top three fixtures ranked by original
grid/geometry evidence, without reading luxel RGB. Source positions, geometry,
support, bounds, policy, code and executable hashes freeze before numerical
scoring. A native ray along each fixture's original outward direction selects
the first known geometric opaque receiver within 768 WU. The ROI includes source
and receiver, with 192 WU tangential and 32 WU axial margins. Only that exact
original receiver surface is eligible for fitting. Unknown/mover-dependent hits
cannot define a receiver. The ray's farther endpoint can be outside the BSP;
only the known opaque hit is used, never an assumption that the remainder is
empty.

Geometry-only coverage uses zero dummy RGB and the same scoped occupancy wrapper
as fitting. Unknown coverage is counted and excluded; known blocked zeros remain.
The final winning source has 188 known positive geometric responses and no
excluded unknown responses on its requested receiver surface. The geometry-only
design considers 64 declared origins of 128 WU blocks, selecting material/normal
support by minimum informative fold coverage. A three-times planar per-axis
footprint bound plus 1 WU separates blocks; stable sample IDs cap folds at 48.
Neither brightness nor held-out loss chooses membership. Bake filtering remains
unknown, so the gap is a precaution rather than a proven independence radius.

The fit predicts modern diffuse relative incident response through a fixed,
explicitly conditional NetRadiant encoding and compares with original RGB8
quantization/saturation intervals. It does not decode RGB8 into calibrated
irradiance. The numerical budget is 200 sweeps with a declared `1e-4` relative
robust-loss stopping tolerance. Tight-tolerance and smaller-budget results remain
recorded, including their nonconvergence. Source/background correlation means
convergence to this tolerance does not establish unique lamp power.

The fixed acceptance gates require a converged, positive source; existing
geometric admission and source-side certification; no upper-bound hit; validation
selection; at least 10% held-out MSE and 5% robust-loss improvement; at least three
distinct agreeing original grid records with known-static rays; and a meaningful
frozen source-removal response. No gate is replaced by a nonempty diagnostics
file, model marker, unknown-as-black prediction or optimizer exit code.

| Original licensed BSP | SHA-256 |
|---|---|
| `hoth2` | `3fc5d0a469c7246d09cb9d0be4500fc0f9a5923ca9a23fdee51aba9eceb3f71d` |
| `hoth3` | `821e6413f10e1e4aca7b6a5fd1bc57a6731bce5d9075cb0175208915cee5cb32` |
| `mp/duel4` | `6575914ae34291ae645889a14650184157774a83f094e3fc43feed2a88ff65f0` |
| `mp/duel10` | `6112dfeeb7f224697c3a96794e9c0cb0f05d421c712b64b70c635f11e81de461` |

All **498** selected fitting rows across the original cohort and final proof
match the original BSP lightmap bytes exactly. Their positions match original
face planes, and fold IDs and geometric blocks are disjoint within each ROI.
Original BSP, catalogue, source manifest and capture identities agree. Original
image hashes are checked against the assets0–3 view. An initial catalogue using a
PBR-pack replacement blue image was rejected before scoring and regenerated.

This is an exploratory study, not a blind-human experiment. Successive geometry
and numerical setup corrections are disclosed. Repeated ROIs and overlapping
source hypotheses are correlated and do not provide independent replicate
evidence. The native training/validation/held-out boundary is a trusted
controller/API boundary, not an OS sandbox against arbitrary malicious code.

## Other selected regions and failed studies

The [public receipts](../tools/rend2/stock_aesthetic/results/v1/summary.json) contain
all 12 regions in the original and final proof cohorts: one accepted conditional
candidate, six rejected regions and five unresolved regions. The separate
[attempt history](../tools/rend2/stock_aesthetic/results/v1/attempts.json) retains
every selected exploratory ROI and its protocol/result hashes, including aborted
preflights. Raw failed results remain private; none is overwritten by success.

| Region | Outcome and limitation |
|---|---|
| `hoth2-wall-bank` | Rejected by this conditional fit; uncertain mover/bake occupancy prevents useful visible forward contributions. |
| `hoth2-small-ceiling` | Unresolved: fewer than four separated same-material rows in a fold. |
| `hoth3-ceiling`, `hoth3-blue`, `hoth3-large-ceiling` | Nuisance-only selection on supported rows; unknown scoped geometry is excluded, never cleared. Blue baked evidence is retained. |
| `duel4-yellow-panel` | Unresolved: occupied source support is not certified outside world solids. |
| `duel4-white-panel` | Nuisance-only selection in its original frozen region. |
| `hoth2-exterior` | Native zenith sky hit found; the bounded region has zero eligible planar audit receivers. Numeric sun remains unavailable. |
| Final `duel10-grid-1` | Nonempty admitted source but fails the held-out improvement gate. |
| Final `duel10-grid-3` | Known opaque hit reaches a different original surface; no sufficient separated mapped receiver folds. |

The earlier fixture-centred boxes missed the actual floor and overrepresented
undersides. Their apparent small gains failed frozen source-removal checks. The
geometric report exposed that selection error; using the native receiver hit
fixes it without moving a source or treating unknown geometry as empty. Other
recorded failures include scoped-occupancy mismatches, whole-face source-support
intersections, ASR15 dark-support budgets, inadequate sparse fits and overly tight
numerical stopping. Changing the excessive early power cap alone did not rescue
the failed symmetric-ROI studies. An optional association budget failure is
reported unavailable and does not stop the independent admitted-fixture/grid/fit
path or publish a partial assignment.

## Limits and checks

This demonstrates a useful fixture-associated source hypothesis in a bounded
stock region. It does not identify exact historical position, lamp count, bake
participation, optical shape, spectrum, calibrated energy or full indirect
transport. Neighboring strip hypotheses remain plausible. MP captures use SP
material interpretation; exact MP runtime semantics are not independently
verified. The point proxy, 4 WU stand-off, encoding and comparable-material
response are explicit assumptions. No whole-map or remaster-readiness claim is
made.

The [verification receipt](../tools/rend2/stock_aesthetic/results/v1/checks.json)
records 57 passing CPU regression tests (the unchanged long-running historical
point-refinement test excluded), 34 passing Python checks and seven targeted
CPU checks after the final adapter changes. Poisoned held-out RGB changes
scores but not selection, geometry, fitted powers, nuisance or training/validation
results. Poisoned geometry-only RGB changes no coverage result. Outside/unknown
segments remain unknown. The source-removal replay agrees with native scoring.

All **113** historical core/canonical source files match the ASR19 hashes. The
historical solver and strict `Resolved` meaning, canonical storage/codecs,
renderer, world shading, GPU/runtime consumers and double-lighting behavior are
unchanged. Outputs are private research/debug artifacts, not runtime assets.
