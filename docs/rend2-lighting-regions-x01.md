# X01: local geometry uncertainty on stock Jedi Academy maps

This report describes surface schema **5**, visibility **2**, region graph **2**
on `rend2-unified-wip`, starting at `71b23f943`. The offline container remains
1.0. This is a CPU geometry/input change: no light fitting, reconstructed
lighting publication, new runtime lights or consumer integration occurs.
The numerical witness is
`tools/rend2/lightrecon_baseline/regions-x01-validation.json`.

## Reproduced discrepancy

The old inspector and D09 capture adapters feed the same visibility/region
algorithms. Differences were in source classification, materials and options.

| Stage | Historical inspector | D09 renderer snapshot |
| --- | --- | --- |
| Geometry | Original indexed faces/soups and quadratic patch controls | Same retained original data |
| RBSP type 4 | Nonpolygonal flare explicitly skipped via NoDraw | Unsupported with no vertices |
| Materials | BSP flags; otherwise unresolved shader optics | Actual parsed shader facts |
| Moving models | Original model bounds; degenerate boxes omitted | Original model bounds |
| Patch tolerance | minimum cell / 4, at least .01 | Metadata default 8 world units |
| Historical hoth3 ROI | (3320,700,123), radius 512, coarse/min 128/32 | Different from saved export |
| Saved hoth3 export ROI | - | **(3320,700,123), radius 256**, 128/16 |
| Saved hoth2 export ROI | - | **(1728,-576,1040), radius 256**, 128/16 |
| Actual default export ROI | - | World-AABB centre, radius 256, 128/16 |

The archived ROI bounds reproduce exactly the reported **31,340 / 29,604**
cells. They were not the AABB defaults. Actual defaults were tested separately.

Original BSP SHA256:

- hoth3: `821e6413f10e1e4aca7b6a5fd1bc57a6731bce5d9075cb0175208915cee5cb32`
- hoth2: `3fc5d0a469c7246d09cb9d0be4500fc0f9a5923ca9a23fdee51aba9eceb3f71d`

`lightrecon_regions_audit` verifies the original BSP hash and the retained
surface types, positions, triangle order and patch dimensions. It compares
every canonical domain/dynamic-boundary field against a new `CaptureBspSpace`
extraction. All ten ROIs pass these checks. It builds offline replay and a
direct graph on identical leases/options and compares every cell's ID, class
and reasons. Typed geometry/domain signatures and all policy limits are recorded.

Controlled comparisons use patch tolerance 8, coarse 128, minimum 32 or 16,
four sky samples, two-sided finite rays, production tolerances (1e-5 world
absolute, relative ray bias 4*float epsilon, cap .05), 400,000 nodes,
350,000 cells, 2,000,000 links, 1 Mi work units per ray, visibility work
128 Mi units, graph work 256 Mi units, visibility/graph budgets 256 MiB and
caller budget 512 MiB. Archived-options replay retains its 128 MiB / 128 Mi
graph limits. Actual renderer export keeps 128 MiB BVH / 256 MiB caller limits.
The retained scenes fit all these limits; budget differences do not change a
successful geometry build. The v1 witness adapter uses the matched 256 MiB
policy as well.

The unmodified original inspector was run again and reproduced **9 regions,
5 components, 1,396 Free / 641 Solid / 24,850 Unknown**.
All **26,887 serialized cell rows**, including coordinates, reason bits and IDs,
are byte-identical to the flare-only v1 replay of the actual D09 snapshot.

| v1 controlled input | BSP-Free leaves before visual check | Local invalidations | Global-only invalidations | Free / Solid / Unknown | Regions |
| --- | ---: | ---: | ---: | --- | ---: |
| Raw D09, historical hoth3 ROI | 3,285 | 104 | 3,181 | 0 / 641 / 28,031 | 0 |
| Only verified flares excluded | 1,500 | 104 | 0 | 1,396 / 641 / 24,850 | 9 |
| Inspector material policy too | 1,500 | 104 | 0 | 1,396 / 641 / 24,850 | 9 |
| Raw D09, saved hoth3 export ROI | 5,725 | 419 | 5,306 | 0 / 2,958 / 28,382 | 0 |
| Raw D09, saved hoth2 export ROI | 23,215 | 32 | 23,183 | 0 / 2,601 / 27,003 | 0 |

Adaptive leaves are not equal-volume populations: Free boxes stop refining.
Volume comparisons below are clipped to the exact ROI.

hoth3 has 54 flares and hoth2 98. Under the old moving/NoDraw exclusion order,
19 hoth3 and 98 hoth2 records reached `unboundedUnsupported`. This made
`Report().complete=false`; `Subdivide` then invalidated every BSP-Free box.
Correcting only verified flare semantics exactly reconciles the historical
result. Materials still affect optical rays: unresolved inspector shaders
cannot prove the opaque contacts available from actual renderer material facts.

## Geometry semantics and completeness

| Source category | Treatment |
| --- | --- |
| PLANAR / TRIANGLE_SOUP | Original indexed geometry; degenerate triangles remain bounded uncertainty |
| PATCH | Deterministic quadratic tessellation and conservative chord/rounding envelope |
| FLARE | Appended `BakedSurfaceKind::Flare`; no static occluder |
| Moving model / deformation | Excluded from immutable static triangles; dynamic model bounds and unknown swept extent stay distinct |
| Known NoDraw | No rendered occluder; collision brushes can still classify Solid |
| Alpha-test / translucent | Geometry retained; transmission Unknown unless explicitly assumed by the caller |
| Unsupported with established bounds | Bounds-only uncertain BVH primitive |
| Malformed / future type without bounds | Unbounded potential blocker |
| Unknown material | Geometry retained, local opacity/sidedness uncertainty |

Flare exclusion follows the RBSP type, not brightness, distance, shader name or
missing vertices. `ParseFlare` consumes origin/colour/normal effect data, not
an opaque polygon mesh. No physical light is inferred. Empty Unsupported
records are never automatically upgraded: old archives need the hash-matched
original BSP to recover that source fact. Normal flare rendering is unchanged.
Flare records also stay out of luxel mapping: their irrelevant lightmap number
must not create a full-chart Unsupported mask. A fixture verifies unchanged
64-luxel quad coverage beside a flare, while a genuinely unknown surface still
keeps the conservative mask.

For manually supplied Unsupported records, nonempty vertices explicitly assert
that their AABB bounds all possible geometry. Suppliers without that evidence
must leave them unbounded. The RBSP adapter does not populate guessed hulls
for malformed/future types. Unsupported alone never proves harmlessness.

`Report().complete` now records global source-record support; bounded
unsupported/degenerate primitives also keep it false. The separate
`spatialCoverageComplete` means no possibly relevant unbounded static occluder
is omitted. It does not promise known optics, exact curved intersections or
absence of dynamic objects.

`OverlapBox` exposes local reasons and coverage. Triangle/box separating-axis
tests replace triangle AABB-only tests. Patch error expands the query box
conservatively; bounds-only unknowns retain their entire AABB. A local miss can
therefore be certified beside an unrelated bounded unknown, while an unbounded
possibly opaque omission still prevents Free/Clear claims. A known opaque wall
still proves Blocked in an incomplete scene. Sky requires a real sky contact.

## BSP-domain and adaptive handling

`Locate` retains its semantics: a deep containing model-0 solid brush gives
Solid/Sky; near boundaries stay uncertain; a nonnegative-cluster leaf outside
solid brushes can certify Free; cluster -1 without a solid proof stays Unknown.
PVS, light styles, RGB and absence of triangles certify none of these states.

`InspectBox` reports outside/partial world bounds, unclustered leaves, possible
brush intersection and full containment. It visits all intersected leaves.
Structural splits between ordinary free leaves do not create rooms.

After plane/AABB exclusion, at most four interval-clipping passes propagate
necessary bounds for the conjunction of brush inequalities:

```text
n[k] * x[k] <= d + tolerance - sum(j != k, min(n[j]*[low[j],high[j]])).
```

Double bounds are rounded outward with the existing 1e-5 world tolerance.
Only inconsistent necessary intervals prove disjointness. Non-convergence
retains Unknown; this is not an exact convex-polyhedron solver. Work limits
include clipping; it uses fixed local arrays and no query heap allocation.
A synthetic oblique wedge and 400 boxes checked against independent sampled
half-space membership verify this refinement.

Only spatial uncertainty drives subdivision: mixed BSP boxes, local geometry
and moving bounds. A solely unbounded omission cannot be resolved by smaller
cells and remains coarse Unknown. Minimum-resolution boundary cells remain
Unknown. No globally extreme grid resolution or free-space guessing is used.

The cell cover can extend past a non-aligned ROI. Volumes are clipped to the
requested ROI; outside-ROI queries explicitly report unavailable assignment.
Partial world-bound boxes stay conservative. No fractional threshold turns a
partly solid box into Free.

## Diagnostics, optional assignments and lifetime

Reports count BSP Free/Solid/Unknown before visual checks on final adaptive
leaves, local/global-only invalidations, minimum limitation, moving, optical
and unsupported uncertainty, representative differences, locally clear but
unassigned cells, unclustered leaves, possible brushes and refined disjointness.
Reason counts may overlap. TSV cell bits distinguish these causes.

`representative_class_differences` includes expected BSP-box/BSP-point
differences at mixed boundaries. `free_certificate_point_disagreements`
detects the stronger inconsistency of a Free box with a non-Free centre;
that counter is zero on all tested ROIs. Visibility metadata distinguishes
bounded unsupported, degenerate, unbounded/missing bounds, flares, moving
surfaces and NoDraw.

`Lookup` and grid/surface assignments retain Unknown with region 0 and
diagnostics. `outsideAnalysisBounds` distinguishes unavailable ROI coverage
from physical Outside. Unknown membership never erases a valid observation.
`FilterPair` always queries actual static geometry even with unavailable
regions. Same region is not LOS; disconnected sampled components are not proof
of a wall. Dynamic intersection and unknown travel have separate flags.
Actual source-to-luxel queries should use `RayQuery` target IDs/tolerance and
normal bias; do not reject valid samples merely for missing region IDs.

Input leases, BVH and graph remain immutable, with bounded scratch and no
shared mutable ray cache or background thread. Source buffers can be released
after BVH construction. Hashing uses 4 KiB scratch. Memory reports distinguish
owned/build storage from total audit-process peak (archive, original sources,
two graphs, debug/reference geometry).

## IDs, schema and cache safety

Ordinal IDs are scoped by a content certification key: map hash, visibility
schema/options, source positions/indices/types/motion/material signals/cull,
canonical BSP tree/leaves/planes/brushes, dynamic boundaries and graph
options/limits. Geometry or interpretation changes invalidate it.
Custom domains without canonical BSP content have lease-local IDs;
`PortableCertification()` is false and offline graph export rejects them.

Export checks the same immutable geometry/domain leases and unchanged dynamic
records. A weak geometry lease performs this check without retaining buffers.
Replaced or expired inputs require explicit replay, even for an identical
replacement object. Stale IDs cannot be relabelled beside changed geometry.
Tests reject stale export, verify a changed key after replay and reject dynamic
metadata mismatch.

The offline container remains 1.0 with surface/region sub-schemas 5/2.
Region sections append counters and the key. Legacy 4/1 readers label the
imported version; they invent neither flare semantics nor current certification.
`RegionSchemaVersion()`, `SurfaceSchemaVersion()`, `RegionSchemaCurrent()`
expose that state. Old luxel region references belong to their archived schema.
Explicit replay builds new results; re-export requires a matching fresh graph.
The reconstructed output-asset/cache format is unchanged.

## Stock coverage and performance

Volume units below are millions of world units cubed. These are static geometry
certificates, not recovered lighting.

<!-- X01_COVERAGE_TABLE -->

| ROI | Free / Solid / Unknown cells | Free / Solid / Unknown volume (Mwu³) | Regions / components / apertures | Rays V / B / U | Grid assigned / present |
| --- | --- | --- | --- | --- | --- |
| hoth3-default | 1554 / 1997 / 25654 | 16.544 / 12.595 / 105.079 | 11 / 9 / 2 | 44 / 212 / 0 | 25 / 256 |
| hoth3-historical | 1446 / 641 / 24786 | 105.472 / 21.004 / 791.029 | 10 / 5 / 7 | 60 / 193 / 3 | 103 / 1536 |
| hoth3-export | 2212 / 2958 / 22747 | 23.081 / 17.965 / 93.172 | 3 / 2 / 1 | 102 / 128 / 26 | 9 / 256 |
| hoth3-west | 0 / 0 / 32768 | 0.000 / 0.000 / 134.218 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 256 |
| hoth3-upper | 0 / 0 / 32768 | 0.000 / 0.000 / 134.218 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 256 |
| hoth2-default | 3049 / 15384 / 4640 | 29.090 / 86.122 / 19.005 | 4 / 3 / 1 | 256 / 0 / 0 | 14 / 120 |
| hoth2-export | 1333 / 2601 / 3788 | 95.089 / 23.613 / 15.516 | 1 / 1 / 0 | 255 / 1 / 0 | 50 / 100 |
| hoth2-east | 95 / 9162 / 9567 | 0.389 / 94.642 / 39.186 | 1 / 1 / 0 | 253 / 0 / 0 | 0 / 120 |
| hoth2-courtyard | 20386 / 10240 / 2142 | 83.501 / 41.943 / 8.774 | 2 / 0 / 0 | 256 / 0 / 0 | 50 / 100 |
| mp_ffa3-spawn | 769 / 891 / 24080 | 26.919 / 8.667 / 98.632 | 2 / 2 / 0 | 49 / 0 / 207 | 0 / 0 |

| ROI | BVH MiB | Region peak estimate MiB | Process peak MiB | Region build seconds | BVH + region seconds |
| --- | ---: | ---: | ---: | ---: | ---: |
| hoth3-default | 36.76 | 24.75 | 97.70 | 0.475 | 1.441 |
| hoth3-historical | 36.76 | 22.73 | 97.45 | 0.570 | 1.561 |
| hoth3-export | 36.76 | 24.51 | 97.77 | 0.790 | 1.809 |
| hoth3-west | 36.76 | 25.58 | 97.90 | 0.275 | 1.208 |
| hoth3-upper | 36.76 | 25.58 | 97.98 | 0.369 | 1.316 |
| hoth2-default | 19.92 | 21.59 | 76.75 | 0.673 | 1.173 |
| hoth2-export | 19.92 | 7.81 | 69.84 | 0.415 | 0.938 |
| hoth2-east | 19.92 | 14.79 | 74.09 | 0.591 | 1.086 |
| hoth2-courtyard | 19.92 | 52.06 | 88.24 | 1.179 | 1.709 |
| mp_ffa3-spawn | 20.99 | 21.16 | 56.93 | 0.244 | 0.781 |

<!-- X01_COVERAGE_TABLE_END -->

Historical hoth3 Free volume changes from zero in raw D09, to 103.375 million
with flare semantics alone, to 105.472 million with local geometric refinement.
Its Solid volume remains 21.004 million. The two entirely Unknown hoth3 ROIs
contain 32,768 unclustered-domain cells without a containing-brush proof.
The courtyard-labelled hoth2 ROI has two conditional regions and zero
unconditional components because moving bounds cover it. ROI labels are
reproducible names, not verified semantic room classifications.

Grid assignment uses original present logical cells without style/RGB
heuristics. The saved two-image prefix provides only 16 mapped in-ROI luxels
for historical hoth3; none receive a certified side under the bounded search.
Most other ROIs have no luxels in that prefix. This is not whole-map surface
coverage. Full original images/geometry remain available for tiled mapping and
direct rays. MP is explicitly geometry-only; grid/luxel rates are unavailable.
After the flare chart-mask fix, a fresh renderer export supplies **82** mapped
luxels in the historical hoth3 ROI, with **16** assigned to certified sides.
The same geometry/options key and region counts are retained. The hoth2 export
ROI still has no mapped luxels in that limited prefix.

Up to 256 deterministic segments per ROI compare against independent
plane/barycentric intersection of original opaque face/soup triangles:
535 opaque contacts, zero falsely Clear rays, 533 matching exact terminal
contacts, maximum distance difference below 5e-13 world units. The other two
contacts are not exact-terminal comparisons. Strict triangle edges are excluded
from this numerical witness and tested synthetically. This is not an exhaustive
map proof or analytic-patch reference. MP unparsed optics leave 207/256 rays
Unknown.

OBJ exports group opaque/alpha/translucent/sky/unresolved/moving geometry and
cell meshes. TSV stores cell causes and reference rays. XY/XZ SVG overlays
retain walls, openings and confidence boundaries. Historical hoth3 XY/XZ and MP
spawn XY were visually inspected: wall bands remain unresolved, solid contacts
remain present and clear interiors are bounded by retained geometry. The hoth2
courtyard slice retains pervasive moving-boundary warnings. Licensed map
geometry/images remain in ignored `build/x01/`, never Git.

## Reproduction and actual results

From the repository root (bundled Python was
`C:\Users\Keyten\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`;
GCC/CMake PATH used `C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin`):

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-disk-standalone
cmake --build build/lightrecon-disk-standalone -j 2
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 -j 2
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure
python tools/rend2/datasets/run_pipeline.py --tool build/lightrecon-disk-standalone/lightrecon_dataset_tool.exe --output build/x01/offline-final
python tools/rend2/test_lightrecon_regions_uncertainty.py --tool build/lightrecon-disk-standalone/lightrecon_regions_audit.exe --inspector build/lightrecon-disk-standalone/lightrecon_regions_inspect.exe --datasets build/offline-stock-accepted/run/home/rend2/lightrecon/v1/observations --archives build/offline-stock-accepted/stock-assets/base --output build/x01/proven
python tools/rend2/lightrecon_regression.py --cpp C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/gcc.exe --output build/x01/shader-regression
python tools/rend2/run_gpu_tests.py --only 'test_volumetric*.py' --only 'test_pbr_calibration*.py'
```

Audit syntax:

```text
lightrecon_regions_audit dataset.jkobs original.bsp output-prefix cx cy cz radius coarse minimum sky-samples patch-tolerance raw|flares|inspector
```

`auto auto auto` chooses AABB centre. Radius 0 uses archived ROI/options.
`raw` preserves capture; `flares` recovers only hash-verified type 4 records;
`inspector` additionally uses original BSP flags and unresolved shader optics.
The matrix losslessly gzip-compresses OBJ/TSV after SVG generation.

Frozen original binaries/logs are in `build/x01/before/`. The historical
`tools/rend2/lightrecon_baseline/x01-v1-replay.cpp` was also compiled and run
against an unmodified archive of 71b23f943, reproducing all four key v1 rows.
Its static assertion refuses the current core. With a full base checkout:

```sh
cmake -S <base>/tools/lightrecon_test -B <base-build> -DCMAKE_BUILD_TYPE=Release
cmake --build <base-build> --target rend2_lightrecon_core
g++ -std=c++11 -O2 -I<base>/shared/rd-rend2 tools/rend2/lightrecon_baseline/x01-v1-replay.cpp <base-build>/core/librend2_lightrecon_core.a -o v1-replay
v1-replay hoth3_a.jkobs hoth3.bsp 3320 700 123 512 128 32 8 raw
v1-replay hoth3_a.jkobs hoth3.bsp 3320 700 123 512 128 32 8 flares
v1-replay hoth3_a.jkobs hoth3.bsp 3320 700 123 256 128 16 8 raw
v1-replay hoth2_a.jkobs hoth2.bsp 1728 -576 1040 256 128 16 8 raw
lightrecon_regions_inspect hoth3.bsp prefix 3320 700 123 512 128 32 4
```

The current inspector accepts an optional final filename for a geometry-only
dataset, used for local licensed MP ffa3. It claims neither parsed shader optics
nor full baked observations.

Actual SP exports were run twice per map, master consumption off:

```powershell
python tools/rend2/datasets/test_runtime_export.py --installation "C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData" --binaries build/ssr-msvc/RelWithDebInfo --renderer build/lightrecon-core-renderers/Release/rdsp-rend2_x86_64.dll --tool build/lightrecon-disk-standalone/lightrecon_dataset_tool.exe --private-archives build/offline-stock-accepted/stock-assets/base --output build/x01/runtime-mapping
```

Repeated bytes are identical and validate offline. Saved ROI counts match replay:
hoth3 3 regions / 2,212 Free; hoth2 1 / 1,333 Free. This tests actual capture,
export and ownership, not game-image regression.
Excluding flare from chart masks also changes the two-image prefix's mapped
sample counts from 11,542 to 24,341 on hoth3, and 3,489 to 10,750 on hoth2.
These are recovered observation correspondences, not physical light sources.

Passed: 16 GCC and 16 MSVC CPU targets (surface/patch mapping, lightmaps/grid,
calibration/decoding, core/API/lifecycle/cache, regions, visibility, offline);
both Release renderer DLLs; 11 fixtures generated twice with identical bytes;
27 disabled-path shader programs / 51 stages with zero baseline/interface
differences on Intel GL 3.2; 22 volumetric compute/raster/media permutations and
their numerical comparisons. The calibration GPU glob selects no separate
script; calibration is covered by CPU tests. q3map2 was not rerun for X01.

Negative tests cover a distant flare, distant bounded unknown, unbounded
possibly opaque omission, cutout/glass, known walls surviving incompleteness,
diagonal triangle boxes, near/thin walls, room/door/skylight/patch/recess rays,
moving boundaries, outside-ROI assignment, direct rays without regions,
changed geometry keys, stale export and repeated builds. Old v1 datasets were
loaded in every stock replay; new counters/keys round-trip in format tests.

Intermediate builds ran out of disk/commit capacity; a parallel MSBuild attempt
also ended with OutOfMemory. Following stale CTest results were discarded.
Only X01-generated duplicates were losslessly compressed, then builds/tests
were rerun sequentially with two jobs. Historical partial-source configuration needed the original
SP/MP init/math headers before it built. Existing MSVC x64 /arch:SSE2 warnings
are unrelated. No failed final CPU/GPU test is reported as passed.

## Remaining uncertainty and recommendations

- Unclustered leaves, world-boundary boxes and unresolved brush intersections
  remain Unknown; do not infer occupancy from RGB, PVS or missing triangles.
- Optical masks/transmission, exact curved intersection and dynamic swept
  extents require more evidence. Static Clear is not visibility in every door
  state.
- Unsupported unbounded opaque geometry still constrains the entire scene.
  Bounded unknowns use conservative AABBs.
- Whole-map luxel assignment, arbitrary corrupt compiler spatial indexing and
  an independent analytic-patch reference remain unverified.
- Region graphs are sampled topology, not complete rooms or light transport.
  Valid observations and direct ray fitting must remain usable independently.
- No shader, normal renderer/collision/lightgrid/PBR path or runtime consumer is
  changed. Region coverage is not a successful physical lighting reconstruction.
