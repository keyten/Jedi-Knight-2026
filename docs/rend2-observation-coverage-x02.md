# X02: full-map observation coverage and blind evaluation

This is a data/benchmark milestone, **not lighting reconstruction**. No inverse
solver, recovered physical lights, consumer, shader or gameplay path was added.
Stock maps remain `UnknownProfile`; a valid archive or complete mapping does not
establish physical source positions, photometric scale, or solver readiness.

## Recorded experiments

Licensed local stock assets were loaded by the actual Release SP renderer, with
`r_lightReconUse=0`. `hoth3`, `hoth2` and `mp/ffa3` were each exported **twice with
all colour pages**, imported, remapped and validated offline. The MP map used the
shared world loader through SP; this is not an MP gameplay test. SP and MP DLLs
were built separately. No PK3, BSP, lightmap pixels or map geometry is committed.

The source BSP SHA256 values are:

| Map | SHA256 |
|---|---|
| hoth3 | `821e6413f10e1e4aca7b6a5fd1bc57a6731bce5d9075cb0175208915cee5cb32` |
| hoth2 | `3fc5d0a469c7246d09cb9d0be4500fc0f9a5923ca9a23fdee51aba9eceb3f71d` |
| mp/ffa3 | `2a448530b908761dd797a02f6530bbe2deb6597f1bc58d9c458e6d62619d5b05` |

### Exclusive mapping categories

Every count below has the **original colour-image texel count** as denominator.
The percentages are geometry coverage, not independent measurements or recovered
lighting accuracy. Alternative encodings of a page are not independent energy.

| Category | hoth3 / 622,592 | hoth2 / 196,608 | mp/ffa3 / 425,984 |
|---|---:|---:|---:|
| Colour pages fully derived | 38/38 | 12/12 | 26/26 |
| Mapped centres | 403,084 (64.74%) | 79,565 (40.47%) | 301,473 (70.77%) |
| Footprint intersects; centre uncovered | 27,811 | 9,342 | 9,403 |
| Ambiguous overlaps/coverage | 15,870 | 2,815 | 1,539 |
| Unsupported mapping | 8,145 | 32,351 | 15,170 |
| Certified atlas padding | 92,902 | 46,345 | 58,024 |
| Unmapped chart bounds or possible padding | 74,780 | 26,190 | 40,375 |
| Total | 622,592 | 196,608 | 425,984 |

`MapLuxels::Uncovered` alone is **not** proof of atlas padding. The census checks
source UV bounds, including excluded and movable receivers. Only texels outside
all potentially relevant source chart bounds are called certified padding.
Flares are not baked receiving polygons. A chart bounding box can include padding;
its presence alone does not prove receiver coverage, so that category stays mixed.
Unsupported mapping includes unsupported patch UV interpretation and degenerate
charts; it does not mean the entire source surface geometry is absent. Source
type and motion counts are reported separately in `.facts.json`.

### Additional, overlapping quality categories

| Evidence | hoth3 | hoth2 | mp/ffa3 | Denominator/meaning |
|---|---:|---:|---:|---|
| Affine mapped footprints | 370,876 | 60,031 | 242,085 | All colour texels |
| Approximate curved footprints | 32,208 | 19,534 | 59,388 | All colour texels |
| Oriented geometric normals | 403,067 | 79,565 | 301,473 | Mapped centres |
| Conditional diffuse candidates | 295,197 | 46,626 | 223,787 | All colour texels; still uncalibrated |
| Samples with censored channels | 26,438 | 11,658 | 8,540 | All raw colour texels, including unmapped |
| Censored channels | 26,716 | 13,345 | 8,540 | Three channels per raw texel |
| Available independent deluxe observations | 0 | 0 | 0 | Missing is not zero direction |
| Source-linear comparable RGB | 0 | 0 | 0 | Unknown historical transfer/primaries |
| Unassigned region sides | 620,665 | 196,608 | 425,294 | All candidate texels; padding has no region |
| Potentially moving uncovered chart references | 2,567 | 7,168 | 1,299 | All colour texels |
| Original logical grid cells | 229,068 | 1,044,126 | 94,350 | Full logical layouts, not unique records |

All three raw grid arrays and indexed records are retained. Their direction
encoding is quantized and uncalibrated; neither nonzero RGB nor style zero proves
free space. Ambient/directed are **bake components**, not a physical indirect/direct
decomposition. The archived region graph covers its saved ROI, not the whole map.
An unassigned region does not discard a valid world observation or block a direct
visibility query. Outside-ROI membership is not evidence of a missing wall.

`coverage.json` provides the same taxonomy with explicit denominators per image,
source surface type, material/style, 256-world-unit spatial cell, height bin,
encoded RGB bin and oriented normal bin. The SVGs project mapped observations in
XY/XZ and separately show every page's mapping categories. Unmapped texels have
no invented world-space location. Surface category and source-hint statistics are
in `.facts.json`; complete local tables stay with licensed exports under `build/`.

## Taxonomy and photometric contract

The raw archive and canonical decoder are unchanged. The controller distinguishes:

- Uncovered, certified padding, unresolved chart bounds, mapped, footprint-only,
  overlap and unsupported mapping.
- Finite position, oriented geometric normal, shading normal, positive area and
  approximate footprint. Missing normals and moving receivers have separate reasons.
- Quantized RGB, channel saturation/censoring, unknown transfer, unknown primaries,
  available source-relative and optional calibrated scene-equivalent candidates.
- Material/style inconsistency, bake reflectance uncertainty, separate animated
  styles, missing/unusable deluxe, local geometry uncertainty and region availability.
- A valid observation with **localization not established**. Observation count is
  never used as a confidence or source-position accuracy estimate.

RGB8 255 remains censored/lower-bound information; raw values and channel flags
survive publication. No clipping inversion, material-RGB division, grayscale
presentation, renderer overbright/gamma or tone mapping is applied by these tools.
The decoder recipe remains in the sanitized geometry archive metadata. A missing
`source`/`scene` value is JSON null, not black. Unknown primaries remain incomparable
even when a transfer function supplies a relative linear candidate.

World positions/areas retain world units/world units squared. Normals point outward;
grid/deluxe directions point receiver→source, not photon propagation. `worldUnitsPerMeter`
and `physicalToScene` retain the PBR contract's meanings. They do not reveal the
historical absolute intensity of a lightmap. No baked input is labelled measured
lux, candela or luminance by this milestone.

## Bounded derivation, identity and cancellation

`SurfaceMappingIndex` builds an immutable page→source-surface lookup once. Export
and remap validation reuse it; footprint clipping still uses the established
mapper. Work scales with tiles and the relevant page/receiver geometry, rather
than testing every world surface for every texel. Only one luxel tile and its
serialization buffers are resident. There is no all-luxel decoded matrix.

`Options.mappedImages=0` derives all available colour images. `firstMappedImage`
and `mappedImages` select deterministic ranges among sorted available colour
images, including encoding alternatives. A nonzero start is always partial.
Budget exhaustion is an explicit failure, not a successful truncated archive.
Each shard retains full source snapshots; shards cannot be concatenated as bytes.

`SampleIdentity` hashes a versioned little-endian record containing original BSP
content hash, original image source/pixel hash/encoding/dimensions, logical page,
texel, source surface, style slot and style. Region IDs, partitions, GPU atlases,
tile ordinals and runtime LOD do not participate. Changing map content or external
image content changes identity. Multiple source encodings remain detectable
alternatives; the optimization package selects the first sorted available image
per original logical page, records this policy and never adds alternatives.

The `.jkobs` container remains 1.0; contained surface/region schemas are 5/2 from
X01. Mapping/sample-ID interpretation is v1. Quality and blind package schemas are
`org.openjk.observation-quality.v1` and `org.openjk.blind-observations.v1`.
These are analysis views of existing observations, not new lightmap encodings.

X02 changes `surfaces.h/.cpp` (page index), `offline.h/.cpp` (ranges, progress,
sample identity and index reuse), the standalone CMake/test targets and dataset
audit/runtime scripts. New tools are `lightrecon_dataset_quality`,
`lightrecon_dataset_roi`, `prepare_blind.py`, `solver_view.py`, `evaluation_view.py`,
`make_subsets.py`, `benchmark_report.py`, `plot_coverage.py`, `measure.py`, boundary
and compiled integration tests, plus `write_x02_witness.py`. The capture pipeline,
normal renderer texture transforms, shaders and consumers are unchanged by X02.

Cancellation is checked during index creation and between tiles. The native atomic
writer removes its temporary and retains an existing destination on cancellation.
Observers are borrowed synchronous callbacks and are not serialized. No new map
load work runs unless the existing explicit capture/export policy requests it.

## Actual blind boundary

`.jkobs` is a **trusted controller archive**, not the file to mount for a solver.
Partition tags inside a complete archive are insufficient isolation.

```text
private .jkobs + original images/grid/vertex RGB + answers
                  │ trusted controller only
                  ├── inputs/manifest.json
                  ├── inputs/train.jsonl
                  ├── inputs/grid.jsonl
                  └── inputs/geometry.jkobs  (sanitized whole scene)
                  │
                  └── evaluator/{validation,heldout}[-grid].jsonl
```

The geometry package contains no source lightmap images, grid RGB/directions,
vertex RGB/HDR or source light entity/JSON answers. Vertex colour bytes are zeroed
**and absent**, not presented as black observations. Authored intensities and raw
source-answer evidence are removed. Visible glow geometry and sky boundary hints
remain legitimate priors. This conservative blind policy also withholds authored
JSON/sun/entity priors that a deployment dataset could legitimately expose; the
complete controller archive retains them for a separately declared prior-assisted
benchmark. The redacted geometry package does not claim a complete bake recipe.

`TrainingView` accepts only the training role, expected map hash and checksummed
fixed members. It has no Dataset, raw image, validation or held-out accessor and
no RGB backing buffer shared with the evaluator. It streams caller-owned records;
repeated reads are independent. `EvaluationView` belongs to the harness: validation
is permitted at checkpoints, held-out only after a frozen optimization run. The
harness must strip RGB when requesting predictions. Ground-truth answer files
are never opened by the integration test or published with `inputs/`.

For an untrusted optimizer process, mount **only `inputs/` read-only**, not the
repository, game installation, controller archive, evaluator directory or answers.
Python object privacy is not an OS sandbox; API separation does not claim to stop
arbitrary filesystem access granted by the host. Published files must be immutable.

Spatial splits use fixed world blocks, hash seed and a gap. The full conservative
texel support plus three footprint radii/filter margin must stay inside one block.
Curved patches use their control hull as a conservative support bound. This loses
samples deliberately; it does not guess smaller support from sqrt(area). Duplicate
chart references are excluded. A deduplicated lightgrid record touched by multiple
partitions **or any gap cell** is withheld from every view.

| Published observations | hoth3 | hoth2 | mp/ffa3 |
|---|---:|---:|---:|
| Training surface RGB | 125,944 | 14,154 | 99,605 |
| Validation surface RGB | 11,364 | 10,537 | 19,592 |
| Held-out surface RGB | 37,896 | 3,836 | 25,940 |
| Training grid cells | 856 | 152 | 3,470 |
| Validation grid cells | 29 | 25 | 341 |
| Held-out grid cells | 189 | 8 | 681 |
| Grid cells withheld by shared records/gaps | 227,994 | 1,043,941 | 89,858 |

Stock split: block 1024 WU, gap 8 WU, seed 20261026. These counts are admission
after geometry/style/reference/gap checks, not proof of physical comparability.
Large repeated grid records make the strict blind grid very sparse. Do not recover
withheld values by loading raw sections or interpolating across partition boundaries.

## Information quality and bounded subsets

Per-cell/ROI diagnostics report distinct receiver surfaces, position bounds,
six normal orientation bins and the normalized second moment of normals. The
`27 det(E[n nᵀ])` orientation-span proxy is near zero for coplanar normals; it is
**not Fisher information**, a confidence probability or a Jacobian rank test.

For at most 64 deterministic receiver samples per ROI, the ray tool queries the
same up to eight nearby geometric emitter priors. It reports actual Visible,
Blocked and Unknown counts, candidate distance range and visibility-pattern
diversity. These are geometry probes; candidates are not verified physical lights.
Sky probes require a real sky intersection. An open segment alone is insufficient.

The stock benchmark publisher freezes six spatially distinct stress selections
per map: multiple normals, dense receivers, uncertain materials, bright encoded
RGB near a fixture candidate, weak encoded RGB and upward-facing receivers. It
keeps the original map's split and full sanitized occluder geometry. Names such as
"exterior candidate" do not certify an outdoor region; use the ray report.
Empty/underpopulated subsets explicitly fail admission. Numeric diagnostics and
manifests live outside the solver's input directory.

The selected MP bright-fixture candidate ROI illustrates the limitation: 289 mapped
samples lie on **one receiver surface**, 180 are censored, and all 512 tested
receiver→candidate segments are visible. Its visibility-pattern proxy is one.
More RGB samples there do not establish a unique emitter position. Conversely,
the hoth3 multiple-normal ROI has 20 receiver surfaces but only 6 visible and 6
blocked candidate rays versus 52 unknown rays. Diverse receivers alone do not
remove visibility/model uncertainty. The retained diagnostics expose these cases
rather than ranking every dense/bright ROI as easy or physically solved.

A 256-WU stock ROI can lie entirely inside one 1024-WU partition block. Such a
subset is a training-only or evaluation-only spatial unit, not a complete local
three-fold benchmark. Its counts remain explicit; use the global map split or
combine compatible ROIs. Controlled room fixtures provide local three-fold cases.

The controlled `point_room`, `door_occlusion`, `skylight`, `sun_sky`, `wall_leak`
and coloured-bounce scenes supply known room, wall/door, exterior and material
stress geometry. The 12 existing q3map2 captures were all re-derived, re-imported,
validated and published twice. They use pinned compiler `2.5.17n-git-d9338309e`;
this X02 run did **not** rebake them. Bake provenance remains with the evaluator.
Their geometry-only split seed 20261032 supplies all three partitions in the
ordinary room scenes; the prior stock seed produced no validation block there.
The tiny compiled grids are entirely withheld by the conservative gap policy.

Metrics must remain separate: known-source recovery against answers (future
solver), held-out observation prediction, geometric plausibility, uncertainty and
coverage. Stock source XYZ accuracy is unmeasurable without independent answers.

## Measurements and local artifacts

Windows PSAPI `PeakWorkingSetSize` is measured on the actual child process, not
estimated from STL capacities. It is a process peak, not a live allocation census.

| Operation | Wall time | Peak working set |
|---|---:|---:|
| Standalone hoth3 all-page derivation | 194.37 s | 92,991,488 bytes |
| hoth3 full quality/grid scan | 162.77 s | 96,481,280 bytes |
| hoth2 full quality/grid scan | 78.73 s | 62,935,040 bytes |
| mp/ffa3 full quality/grid scan | 98.05 s | 58,867,712 bytes |
| hoth3 partition/census controller | 172.94 s | 55,369,728 bytes |
| Actual SP process across all three maps | separate runtime log | 1,322,680,320 bytes |

Some independent scans overlapped, so wall times are reproducibility witnesses,
not isolated throughput benchmarks. Scan logs split source load, visibility/region
preparation, tile scan and grid scan. Export progress logs per-page cost. The SP
peak includes engine/renderer/GL/world resources; it is not CPU-core memory.
Archive sizes are 383,432,688 / 131,855,184 / 259,475,840 bytes respectively.
Text census rows are large but streamed. Optional gzip is lossless and supported
by the Python readers. No new compression dependency is needed.

Local accepted artifacts: `build/x02/runtime/result.json`, `build/x02/final/`,
`build/x02/benchmarks/`, `build/x02/compiled-fixtures-frozen/result.json`.
Intermediate interrupted/failed runs are not accepted results. Some duplicate
archives/intermediate rows were gzip-compressed after checking decompressed SHA256
against the original, to limit local disk use. Source `_a.jkobs` archives remain
directly readable. Versioned summary evidence contains no stock pixel/geometry data.

The direct BSP audit compares original image/grid bytes, indexed records, material
IDs, style channels, vertices, normals, colours and source UVs. Stock files contain
NaNs in unbound UV slots (unused style or no corresponding lightmap). These are
reported separately and compared as absent numeric coordinates; `NaN != NaN`
must not create a false capture failure. Bound, active source UVs retain strict
comparison. No original renderer buffer or captured value was changed for this check.

## Reproduce

From the repository root (single-config paths below; use `Release/` for MSVC):

```sh
cmake -S tools/lightrecon_test -B build/lightrecon-offline
cmake --build build/lightrecon-offline --config Release --parallel 2
ctest --test-dir build/lightrecon-offline -C Release --output-on-failure
python tools/rend2/datasets/test_blind_views.py -v

# Full derivation, or deterministic page-range shard: append first count tileSide.
lightrecon_dataset_quality derive captured.jkobs all.jkobs
lightrecon_dataset_tool validate all.jkobs
lightrecon_dataset_tool audit-source-bsp all.jkobs original.bsp
lightrecon_dataset_quality scan all.jkobs local/map
lightrecon_dataset_quality facts all.jkobs local/map
lightrecon_dataset_quality public-check local/map.public.jkobs unused
python tools/rend2/datasets/prepare_blind.py local/map local/views
python tools/rend2/datasets/make_subsets.py local/map local/views local/subsets
python tools/rend2/datasets/plot_coverage.py local/views/coverage.json local/coverage.svg

# Existing licensed runtime setup, fresh output directory:
python tools/rend2/datasets/test_runtime_export.py --installation GAME_DATA --binaries BINARIES --renderer SP_DLL --tool DATASET_TOOL --private-archives PRIVATE_BASE --output build/x02-runtime-new --mapped-images 0 --maps hoth3 hoth2 mp/ffa3 --timeout 1200
python tools/rend2/datasets/test_quality_pipeline.py --quality QUALITY_TOOL --tool DATASET_TOOL --fixtures COMPILED_INPUTS --output build/x02-fixtures-new
python tools/rend2/lightrecon_regression.py --cpp GCC --output build/x02-shaders-new
python tools/rend2/run_gpu_tests.py --only 'test_volumetric*.py'
```

On this host Python is the bundled runtime, invoked with `-X utf8`; CMake/GCC are
in `OpenJK/build/toolchain/w64devkit/bin`. Exact recorded argv, fingerprints and
per-process measurements are retained in the summary witness. The existing
`verify_q3map2.py` validates bake behaviour against separate answers; it is not a
solver score and was not reclassified as one.

The actual host checks used `cmake --build build/lightrecon-disk-standalone -j 2`,
`ctest --test-dir build/lightrecon-disk-standalone --output-on-failure`, and
`ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure`.
Renderer builds used `cmake --build build/lightrecon-core-renderers --config Release
--target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2`. Logs with `*-final` and the
versioned [summary witness](../tools/rend2/lightrecon_baseline/observations-x02-validation.json)
identify accepted results. The witness contains full argv for measured stages,
per-page counts, source audits, brightness quantiles, types/styles/heights/normals,
18 bounded ROI diagnostics and normalized source-code hashes.

For a complete stock report from the local scans:

```sh
python tools/rend2/datasets/benchmark_report.py --root build/x02/final --source-root build/x02/runtime/run/home/rend2/lightrecon/v1/observations --roi-tool build/lightrecon-disk-standalone/lightrecon_dataset_roi.exe --output build/x02-benchmarks-new
python tools/rend2/datasets/write_x02_witness.py --root build/x02 --output local-x02-witness.json
```

A future optimizer should receive a `TrainingView`, build geometry visibility
from its sanitized archive and request training records sequentially. It must
handle null source-linear values with an explicit bake-profile hypothesis, retain
censoring and material uncertainty, and use direct queries when region IDs are
absent. The external evaluator retains validation/held-out targets and source
answers. No optimizer should instantiate the controller's complete `Dataset`.

## Validation and remaining limits

GCC and MSVC CPU suites pass 16/16, including mapping, patches, overlap/degeneracy,
classification, decoder HDR/unknown/censoring, visibility/regions, corruption and
round trips. New tests cover all-page versus prefix/range coverage, indexed versus
original mapping, stable/cross-map IDs, limits, tile cancellation, physically
separate views, repeated reads, unknown primaries, duplicate samples, alternative
encodings, spatial gaps, shared grid records and evaluator stage permissions.
The compiled pipeline also proves atomic cancellation retains the destination and
checks that public geometry contains no hidden image/grid/vertex/source answers.
SP/MP Release builds pass. Disabled-path comparison compiles 27 programs/51 shader
stages with zero differences; froxel compute/raster/numerical tests are separate.

Known limitations remain real benchmark results: unsupported patch UVs, chart
overlaps, 17 missing oriented normals in hoth3, very sparse blind grids, unknown
historical transfer/primaries/materials/absolute scale, mostly unavailable region
IDs outside the saved ROI and uncertain receiver→candidate rays. The stock ROIs
are stress candidates, not automatically certified semantic rooms. A better
clipped curved-footprint bound can admit more samples without weakening isolation.
External-image source-file audits need supplementary originals; the accepted
stock runs here use embedded RGB8 pages. No new physical fit or gameplay image
regression is claimed from serialization, build or export success.

Future benchmark manifests should freeze map/toolchain/content versions, source
profile hypotheses, allowed priors, sample-ID version, receiver support bounds,
split seed/gap, quality admission, source geometry/visibility policy and evaluator
stage policy **before fitting**. Publish localization error only for known-source
fixtures, and report alternative hypotheses or unobservability instead of success
for underconstrained stock evidence.
