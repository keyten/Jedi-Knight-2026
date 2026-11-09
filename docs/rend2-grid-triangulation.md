# ASR13: original-lightgrid source-location hypotheses

This standalone research tool discovers conditional locations from original BSP
grid directions. It addresses aesthetic source discovery for a future remaster.
Every result remains a hypothesis about a single dominant emitter; it neither
identifies exact historical lamps nor certifies that the aggregate is one lamp.
The historical solver, canonical snapshots/assets, renderer, world shading and
runtime consumers are unchanged by ASR13.

## Implementation and evidence

- `tools/lightrecon_test/lightrecon_grid_geometry_tool.cpp`: read-only `.jkobs`
  bridge using the existing [ASR12 extractor](rend2-original-grid-evidence.md),
  captured BSP spatial domain, and static optical visibility provider.
- `tools/rend2/grid_triangulation/triangulate.py`: deterministic search, explicit
  Unknown results, and separately executed post-search residual diagnostics.
- `fixtures.py`, `frozen_truth.json`, `evaluate.py`: evaluator-side controlled
  fixtures and source answers. The search imports none of these files.
- `diagnostic.py`: standalone SVG projections of original directions, retained
  geometry, and spatial alternatives; optional Pillow PNG preview.
- `validate.py`: repeat checks, quantitative blind evaluation, and compact
  [versioned results](../tools/rend2/grid_triangulation/results/validation.json).

The bridge requires original lightgrid capture. It never substitutes reconstructed
P02 probes, interpolated grid values, lightmap directions, or emitter hints. Grid,
domain and geometry map identities must agree. Original positions retain snapped
BSP lattice coordinates and Z-up world units. Directions point from the receiver
toward aggregate incoming light. Missing records, weak directed signal, malformed
style gaps, non-free/boundary positions, and multiple active styles are withheld
with separate reason counts. One direction shared across styles cannot establish
independent per-style source rays. HDR is not used by this experiment.

`consumer256_unknown` is the default and preserves the existing consumer
interpretation. Compression error stays unknown. `writer255_merge4` explicitly
requests the ASR12 conditional writer/merge interpretation; impossible writer
codes are withheld. This flag does not recognize a compiler or establish Raven's
stock production profile. The relative directed color and strength remain encoded
RGB8 values: strength is arithmetic mean RGB/255, not luminance or calibrated
linear irradiance. The fit uses directions; color is a normalized code-space
coherence cue. It performs no inverse-square intensity fitting or absolute
photometry.

## Search and uncertainty

Partition observations before clustering. SHA256 of map identity plus the exact
original record-byte hash assigns 80% training, 10% validation and 10% held-out
buckets. Byte-identical records, including duplicate records with different
indices, share one partition. Repeated references get one search vote and one
evaluation vote per component/partition. This prevents reference-count inflation;
it does not establish statistical independence of distinct compressed records.
This is a record split, not a spatially independent generalization benchmark.

Training cells can join a component through their 26 immediate lattice neighbors
when styles agree, their direction separation is at most 70 degrees and their
normalized RGB chord is at most 0.35. The expanded connecting segment box must
be certified free, and the optical segment must be geometric and clear without
issues. Blocked and Unknown paths never connect components. Unknown dynamic bake
occupancy remains explicit: these are conditional **static** connections and can
never certify the bake pose. Geometry-only connectivity does not depend on the
validation or held-out directions/colors.

Each component gets deterministic pairwise line-intersection proposals and a
whole-component least-squares proposal. Fits minimize perpendicular ray residuals
with angular-distance normalization and robust reweighting. A candidate must lie
at positive distance along every supporting incoming ray. The nominal 6-degree
angular search gate identifies coherent support; it is an empirical search policy,
not a calibrated measurement-error bound. Four distinct original records are the
minimum. Disagreeing support sets can survive as separately ranked alternatives.
At least 60% of the searched component records must support an alternative before
it can be called a `conditional_location`. Minority alternatives remain `unknown`.

Candidate centers are checked against the captured domain and every supporting
optical path. A solid center or definitely blocked path rejects the proposal.
Missing, approximate, optical-uncertain or otherwise unsupported geometry keeps
the alternative Unknown. A clear static path never resolves dynamic occupancy.
Candidate-center geometry and the larger region's geometry are reported
separately; the region is not silently assumed free because its center is free.

The report includes angular RMS/max and whole-component RMS, positive ray
distances, relative color residual, relative strength range, support, normal-matrix
condition and angular-matrix condition. For
`H = mean((I - d d^T) / distance^2)`, the local angular envelope has principal
axes `angular_allowance / sqrt(eigenvalue(H))`. There is no `1/sqrt(N)` confidence
shrinkage. The default allowance covers consumer encoding only; its compression
bound remains unknown. The explicit writer/merge allowance is 16.9412 degrees.
Neither includes unknown sample nudges, interpolation, reduction of multiple
lights, or bounce. The axes are a **local linearized sensitivity envelope**, not
a certified cone-feasible set, probability ellipsoid or global position bound.
`full_uncertainty_known` and `global_containment_certified` remain false.

Nearly parallel/rank-deficient normals (condition above 100), or an angular
envelope whose major axis reaches the typical receiver distance, leave depth
unknown. Unknown bundles expose a diagnostic origin/axis with no upper depth
bound; this axis is not a certified source volume. Unavailable geometry,
insufficient independent records, failed consensus and exhausted work limits are
also explicit. All confidence labels are `conditional_only` or `unknown`.
An excellent fit alone cannot exclude a reflected or multi-source virtual emitter.

Search is bounded to 4,096 cells per requested ROI, 192 distinct search records
per component, 512 pair proposals, 128 components and four alternatives per
component. Record sampling and component budget limitations are reported.
An oversized ROI fails explicitly rather than emitting an unmarked prefix.
Geometry rays have a 65,536-work limit; local diagnostic meshes have a
20,000-triangle limit. Provider errors propagate rather than becoming empty space.

## Blind evaluation

The frozen search object is hashed before evaluation. Validation and held-out
observations cannot enter `discover()`. Evaluation neither refits nor changes
candidate selection, rank, support, confidence or geometry status. It reports
angular/color residuals and blocked/Unknown paths for each partition in a fixed
support-derived spatial window, without nearest-candidate assignment. Nominal
direction-gate outcomes are screening diagnostics, not physical model proofs.

Source answers are read by the evaluator, never by search. The actual q3map2
answer files are opened only after each fresh-process search/repeat has completed.
Their BSP SHA256 and original grid/index lump hashes must match the capture.
The 24-WU truth tolerance is frozen in the analytic truth file and used for both
test suites. `recalled` measures positions among **all ranked alternatives**;
`conditional_recalled` separately counts positions that pass the model/geometry
gate. `false_candidates` counts all spatial alternatives beyond 24 WU of every
truth source, including Unknown alternatives. `false_conditional_candidates`
counts the admitted subset. An unresolved bundle with no center is not counted
as a positioned candidate. These are center-recall metrics, not region-coverage
claims.

### Measured results, 2026-11-09

The 18 Python tests pass, including positivity, near-parallel depth, solid and
Unknown geometry, blocked connectivity, color alternatives, record correlation,
input-order determinism, dynamic uncertainty, unknown compression, and rejection
of validation/held-out search input. Mutating all non-training directions, colors
and positions leaves search output unchanged. Six focused CTest targets pass:
raw lightgrid, ASR12, ASR13, visibility, regions and offline. The new C++ bridge
also passes GCC C++11 `-Wall -Wextra -Werror -pedantic` syntax checking and builds
with MSVC. No GPU/runtime or fresh compiler invocation was used.

Analytic fixtures quantize independently generated aggregate directions into
original-byte-like observations. Their photometry is a controlled relative
surrogate, not a verified compiler forward model. Four point and two spot fixtures
recover **6/6** sources, with **0** false alternatives and 2.39–3.72 WU center error.

| Analytic negative | Spatial recall | Conditional locations | False alternatives | False conditional |
| --- | ---: | ---: | ---: | ---: |
| Two white lamps, aggregate | 0/2 | 0 | 4 Unknown | 0 |
| Two colored lamps, aggregate | 1/2 | 0 | 3 Unknown | 0 |
| Reflected virtual source inside solid | 0/0 | 0 | 0 | 0 |
| Indistinguishable free virtual-source field | 0/0 | 1 | 1 | 1 |
| Direct sun | 0/0 | 0 | 0 | 0 |
| Sun plus local lamp | 0/1 | 0 | 1 Unknown | 0 |

The reflected cases are analytic virtual-source direction surrogates. The free
surrogate intentionally has the same directional field as a direct point source:
its false conditional location demonstrates non-identifiability. Geometry rejects
the solid virtual source. The compiled one-bounce scene below additionally tests
real q3map2 reflected-light evidence. Multiple-lamp and sun/local alternatives
fail the single-dominant-emitter consensus gate. None of these cases gets high
confidence.

Six reused, previously verified **actual q3map2** bakes give these blind results:

| Compiled fixture | Spatial recall | Conditional recall | Best matched errors (WU) | False alternatives |
| --- | ---: | ---: | --- | ---: |
| Point room | 1/1 | 1/1 | 1.53 | 0 |
| Spot | 1/1 | 1/1 | 2.25 | 0 |
| Two colored lamps | 2/2 | 1/2 | 10.20, 4.15 | 0 |
| Overlapping white lamps | 1/2 | 0/2 | 4.15 for matched lamp | 0 |
| One-bounce colored scene | 1/1 | 1/1 | 0.89 | 0 |
| Sun and sky | 0/0 | 0/0 | No positioned candidate | 0 |

Thus ranked position recall is **6/7**, while conditional recall is **4/7**.
Compressed reuse, static geometric support and consensus limit admission. These
small controlled scenes do not establish stock-map localization accuracy.

### Stock visual/geometric diagnostic

The private hoth2 local ROI is `[6144,-5632,512]` to `[6656,-5120,1024]`, frozen
from the earlier geometry-only local-v3 selection. It has 120 original cells;
61 are eligible (48 training references, 3 validation, 10 held-out). Thirty-seven
are withheld for spatial support and 22 for weak directed signal.

Two ranked spatial alternatives remain **Unknown**, with training support
7/16 and 5/16 distinct records and nominal support RMS 2.46 and 3.22 degrees.
Their independent held-out RMS errors are 46.39 and 52.51 degrees, respectively.
All candidate paths remain Unknown; compression and dynamic occupancy are also
unknown. Their centers are not historical lamp identifications. The generated
`build/asr13/final/hoth2-local.svg` and optional `.png` show XY/XZ projections of
captured triangles, original training/validation/held-out arrows and the two
explicitly Unknown alternatives. The image was visually inspected. Its retained
mesh has incomplete local certification and issues 80; the plot labels that fact.

The outdoor ROI around the earlier geometry-only ASR07 sky origin contains
75 cells, with 30 eligible references sharing **one** original record. Its output
is Unknown for insufficient independent records, with no finite-depth source.
The retained local mesh is complete for that diagnostic export. Both ROI reports
repeat identically in separate processes. There is no frozen stock lamp truth,
so stock position recall and false-source counts remain Unknown. Geometry/PNG/SVG
stay in private build output; only compact numeric diagnostics are versioned.

## Reproduction

Python 3.8+ is sufficient for search and evaluation. Pillow is optional for PNG;
SVG generation uses the standard library. With the existing standalone build:

```powershell
cmake --build build/lightrecon-standalone --config Release --parallel 4 --target lightrecon_grid_geometry_tool
ctest --test-dir build/lightrecon-standalone -C Release -R '^(lightrecon_grid_evidence|lightrecon_grid_triangulation|lightrecon_lightgrid|lightrecon_visibility|lightrecon_regions|lightrecon_offline)$' --output-on-failure
py -3 tools/rend2/grid_triangulation/test_triangulate.py -v

$tool = 'build/lightrecon-standalone/Release/lightrecon_grid_geometry_tool.exe'
$stock = 'C:/Users/Keyten/AppData/Local/Temp/jk-lightrecon-all-stock-20261031/runs/hoth2/all-pages.jkobs'
py -3 tools/rend2/grid_triangulation/validate.py --tool $tool --captures build/q3map2-runtime-verified/inputs --suite build/q3map2-bake-verified --stock-capture $stock --output build/asr13/final --summary tools/rend2/grid_triangulation/results/validation.json

# One bounded original-grid discovery, unknown stock compression/profile:
py -3 tools/rend2/grid_triangulation/triangulate.py --tool $tool --capture $stock --roi 6144 -5632 512 6656 -5120 1024 --output build/asr13/local.json --diagnostic build/asr13/local.svg
```

The existing private captures/bakes are prerequisites; see the
[capture/validation recipe](rend2-q3map2-validation.md). CMake was available locally
at `../OpenJK/build/toolchain/w64devkit/bin/cmake.exe`. CTest registers the Python
suite when a Python 3.8+ interpreter is available during configuration. The bridge
prints metadata, then accepts one ASCII command per line: `cells`/`box`/`mesh`
plus six box coordinates, `point` plus three coordinates, `ray` plus six endpoint
coordinates, and `quit`. Replies are one JSON object per line. It writes no files.
