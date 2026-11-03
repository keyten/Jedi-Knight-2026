# P02: generated geometry support and safe CPU spatial interpolation

The new CPU `Probes` module builds adaptive **geometry-only** support independently
of inverse source fitting. A valid topology with absent values is not a lighting
field. Nothing invokes it from map loading or changes world/entity/fog lighting.
Original lightgrid locations, RGB and direction records are neither inputs to
placement nor mutable outputs. Stock source truth is still unavailable.

## Geometry and placement

`Input` binds map SHA, immutable `StaticVisibility::CertificationKey`, explicit
domain generation and optional `LightingRegionGraph` lease. Build rejects stale
map/geometry/region combinations. Generated IDs hash map identity, original
adaptive box, level and record role; source generation/content is separate.
The original and final position, parent/cell, level, BSP leaf/area, region or
explicit unassigned state, supported free box, issues and relocation distance
are retained. Cell records retain solid, unknown and unresolved clearance areas.
There are no cloned historical observations or invented physical emitter IDs.

The bounded octree splits geometry-intersecting or unknown boxes down to declared
minimum spacing. Free uniform boxes stop at coarse spacing. Geometry/source/
coverage/angular-error/uncertainty hints request finer spacing over explicit
bounded supports; they contain no hidden RGB. Removing a refinement request
coarsens by a deterministic rebuild. This first version accepts error requests
from a caller; it does not automatically discover stock lighting error or sources.

A uniform domain Free/Exterior box **and** a conservative BVH `OverlapBox` miss
with complete relevant static geometry are required. A point classification or
DDGI relevance score alone cannot certify a cell. Curved error envelopes, unknown
alpha/translucency and unsupported geometry exclude intersecting support.
No global requirement for a complete certified room graph is imposed.

At minimum resolution a generated seed that is already domain-Free may try its
centre and six axis displacements, bounded by a declared fraction (default .25)
of cell width. A local clearance box and a Geometric visible movement segment
must both pass. It cannot escape an initial Solid/Unknown seed or cross a wall.
If no local certificate exists, the cell stays unavailable. This conservative
first version deliberately rejects many seeds that a more expensive nearest-free
method might recover. Values are independently bound to final position and
topology key; carrying old illumination after relocation is rejected.

## Interpolation and visibility semantics

For every usable cell, preparation selects up to eight nearby valid probes
(maximum sixteen), using a BVH over adaptive leaves. A neighbour is admitted only
if the **entire AABB joining the cell's supported query box and the neighbour's
certified support** is uniform domain-Free/Exterior and free of retained visual
geometry. This strong, conservative certificate guarantees each corresponding
segment lies in supported static geometry. It can follow a sufficiently resolved
door/window aperture; it cannot blend through a known partition. Different region
IDs do not imply blockage, and identical IDs do not certify visibility.

```
b_i(x) = 1 / (1 + |x-p_i|^2 / h_cell^2)
w_i(x) = b_i(x) * admitted_static_support_i * optional_moment_weight_i
sample = sum_i w_i value_i / sum_i w_i
```

The denominator must be positive and meet the declared surviving-support fraction
(default .01). There is no positive floor for blocked weights. An empty support,
all-zero visibility weight, missing required value, uncertain runtime door or
unsupported geometry returns **Unavailable**, not successful darkness. Present
zero coefficients remain a valid explicit zero. Exact probe-position queries use
the value at that position. Otherwise this version uses bounded inverse-distance
blending; it does not guarantee affine precision, C1 continuity or a tetrahedral
linear interpolant. Stencil changes may introduce small value/gradient seams.
Coverage holes near walls are reported rather than extrapolated across them.

Distance-moment metadata is optional, directional and separate from admission:

```
variance = second - mean^2
Cantelli(d) = 1, d <= mean
             variance / (variance + (d-mean)^2), otherwise
```

A negative variance beyond 64 double-precision ulps at the mean-squared scale is
invalid; only roundoff-sized negative variance is rounded to zero. Out-of-bin or
out-of-range queries are unavailable. A nonzero statistical bound is not proof
of any deterministic ray. Moments cannot admit a neighbour rejected by geometry,
certify free space or estimate source confidence. They are disabled by default;
this task does not fabricate depth statistics for stock probes. The native test
preserves the failure example: normalizing one occluded statistical weight of .5
with brightness 10 would still return brightness 10. The geometric gate rejects it.

Runtime motion is independent of static support. Known swept boxes mark affected
support conditional; unknown swept extent marks its potential support conditional
without inventing a finite trajectory. Default lighting queries decline it.
Explicit geometry diagnostics may request conditional static support and retain
that issue. Moving-door state, exact source visibility and sky exposure are not
inferred from a local static box. A geometry generation change requires rebuild.

## Values, quantities and publication

Spatial `Topology::Lookup` returns IDs and weights and is angular-representation
neutral. `Prepared` supplies the existing `CpuEvaluator`/`Snapshot` boundary using
P03 world-frame SH and SG values. All channels blend linearly in declared quantity,
unit and calibration; no DDGI display gamma, E/pi conversion or exposure is added.
After a float `Vec3` passes the canonical unit tolerance, the accepted direction
is expressed as a double unit vector for P03's stricter basis arithmetic. Invalid
or unknown directions are rejected, not repaired. The non-axis directional RGB
regression covers this float/double boundary.
Irradiance-response coefficients are never read as incoming radiance. The adapter
supports already convolved SH irradiance and SH/SG incoming radiance; SG diffuse
quadrature and radiance-to-irradiance conversion remain offline P03 operations.
Negative angular SH evaluations return Unsupported rather than an unreported
energy-changing clamp. Automatic field selection requires Resolved field metadata;
candidate fields need explicit handles and cannot resolve alternative groups.
For SG neighbours, the adapter blends evaluated radiance, not lobe axes or widths;
this preserves the linear quantity without averaging incompatible directional
parameterizations. Irradiance coefficients and radiance lobes are not combined
as if they had the same units.

`Prepare` validates the actual canonical asset, linear Rec.709 declaration,
selected/exclusive membership, geometry/topology generation and every value's
evaluated final position. Root/path/configuration/composition metadata remains on
the canonical `TransportField`; it is not merged or discarded by interpolation.
Unknown membership gives no permission to add physical lights or replace a bake.
Relative values remain relative. There is no RGB8-to-lux adapter here.

## Persistence and cost bounds

Portable CPU chunks use version 1, little-endian scalar encoding, SHA-256 checks,
bounded records and distinct dataset IDs. Geometry chunks contain 128 probe
records; topology chunks contain 128 cell records and their variable bounded
neighbour certificates. They carry map/domain/policy/generation identities.
Lighting coefficients are separate P03 values. The existing `Disk::Chunk` and
generic dataset directory supply storage; no new GPU texture layout or canonical
asset wire-schema change is required. Decode validates counts, references,
certificates, generations and content hash, then rebuilds the spatial index.
Missing chunks are corrupt/incomplete rather than implied zero records.
These are integrity/generation checks, not authentication of arbitrary hostile
geometry certificates. A loader must use trusted producer output and the
canonical container's expected chunk/content identities.

Default bounds are 100k adaptive nodes, 20k probes, 160k links, 64 MiB owned
topology budget and 32M deterministic preparation work units. Node/probe/link,
memory/work/ray exhaustion and cancellation return explicit failure with no
half-published topology. Applications can schedule bounded ROIs; completion is
scoped to those bounds, never a whole-map absence certificate. Owned byte counters
exclude shared source geometry and allocator overhead; measured process RSS is
reported separately. Ordinary queries use a bounded local stack and fixed arrays:
no allocation, I/O, lazy caches, rays or renderer calls.

## Reused algorithm evidence

The existing [DDGI investigation](rend2-ddgi-field-research.md) and pinned SDK
sources were read, including `ProbeRelocationCS.hlsl`, `ProbeClassificationCS.hlsl`
and `Irradiance.hlsl`. DDGI's weight floors and nonlinear display interpolation
are intentionally not copied into canonical physical fields. The existing
[RBDOOM study](rend2-rbdoom-field-research.md) and pinned
`RenderWorld_lightgrid.cpp` support region-local placement and explicit invalid
grid handling, not an emitter estimator. This implementation is independent CPU
code; no RTXGI/RBDOOM dependency, hardware tracing, shaders or licensed assets
are added. [P03](rend2-directional-fields-p03.md) remains the angular codec policy.

## Reproduction

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc -j 2
build/s06-gcc/lightrecon_probes_test.exe build/p02-native.json
python -X utf8 tools/rend2/probes/run.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/probes/results/v3
python -X utf8 -m unittest tools.rend2.probes.test_reports
ctest --test-dir build/s06-gcc --output-on-failure -j 2
$env:CL = '/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

Use a fresh output revision: frozen result folders are not overwritten. The stock
protocol predeclares hoth3, vjun2 and mp/duel4, three geometry-ordinal-quantile ROIs
per map, 512-world-unit boxes, 128/32 spacing, 1-world-unit local clearance and
1,024 independent geometry query positions per ROI. These are bounded spatial
coverage experiments, not full-map lighting truth or inverse source fitting.
No target RGB chooses ROIs or refinement. BSP/dataset/executable/source hashes,
failures and resources are retained; proprietary source payloads stay outside
published results. Numerical results and remaining coverage follow below.

## Numerical results and interpretation

Final versioned witness: `tools/rend2/probes/results/v3/manifest.json`.
The native test has **8,274 assertions**, no failures, and identical repeated
semantic output. Performance counters are kept separate from deterministic
numeric results. Tests include opaque/alpha walls, doorway and window apertures,
multi-storey and tall rooms, stepped curved-corridor geometry, outdoor support,
generated relocation/rejected relocation, sparse/missing values, bounded work,
cancellation, stale geometry/positions, corruption and incomplete chunks.

| Controlled quantity | Result |
|---|---:|
| Constant RGB absolute error | 0 |
| Linear spatial RGB gradient RMSE | 0.110826 |
| Spatial directional-colour L1 field RMSE | 0.00278511 |
| Known point, fixed upward-normal irradiance RMSE, coarse | 2.35675 |
| Same independent positions, refined | 0.934711 |
| Admitted links crossing sealed thin wall | 0 / 9,409 |
| Valid doorway-crossing links | Present |
| Coarse / refined probes in the scalar test | 64 / 1,408 |
| Relocated generated probes in near-wall fixture | 256 |
| Maximum accepted relocation | 0.25 world unit |
| Coarse owned geometry/topology bytes | 41,956 |

The point reference is `E(x,+z)=100 max(0,7-z)/r^3`, source at (-2,0,7),
with world-unit relative amplitude and no singular sample. Its scalar values are
used only to test spatial interpolation; they are **not** serialized as physical
indirect GI. The independent 200 query positions use the analytic value, not an
interpolator as ground truth. Refinement reduces this RMSE by about 60.3%, at a
large probe-count cost. Coarse IDW is consequently unsuitable for sharp local
source detail; explicit source evaluation remains preferable. The directional
test varies world-basis RGB coefficients spatially and tests non-axis directions.
Angular compression errors remain the separate P03 benchmark.

Zero coefficients pass as known darkness. Missing dark-room coefficients with
only bright probes behind a wall return Unavailable. No zero/occluded support is
normalized into successful brightness. Unknown alpha geometry has unavailable
support at the partition, and unbounded mover motion makes the otherwise static
support conditional. Current dynamic states are not inferred.

### Stock geometry, not stock radiance

All nine predeclared ROIs complete, retain unknown geometry and round-trip through
the CPU codec. Each ROI has volume 134,217,728 world units cubed. Percentages below
refer to **that ROI volume**, not floor area or map-wide coverage. ROIs may overlap,
so totals are workload counts, not deduplicated full-map physical volume.

| Map/ROI | Probes | Relocated | Region graph regions | Static supported volume | Valid static query points / 1,024 | Strict current-state support |
|---|---:|---:|---:|---:|---:|---:|
| hoth3 / 0 | 777 | 4 | 7 | 10.13% | 109 | 0 |
| hoth3 / 1 | 523 | 69 | 3 | 7.69% | 77 | 0 |
| hoth3 / 2 | 530 | 5 | 4 | 8.23% | 76 | 0 |
| vjun2 / 0 | 926 | 16 | 8 | 19.05% | 202 | 0 |
| vjun2 / 1 | 1,111 | 6 | 4 | 18.36% | 183 | 0 |
| vjun2 / 2 | 444 | 0 | 3 | 19.02% | 188 | 0 |
| mp/duel4 / 0 | 671 | 3 | 1 | 55.37% | 568 | 568 |
| mp/duel4 / 1 | 836 | 2 | 1 | 46.95% | 487 | 487 |
| mp/duel4 / 2 | 718 | 4 | 1 | 37.65% | 390 | 390 |

hoth3/vjun2 static support above is explicitly conditional on excluded runtime
motion; unbounded swept extents prevent strict current-state queries everywhere
in these scopes. This is preserved uncertainty, not a claim that doors are absent
or that the area is dark. mp/duel4 has no such conditional probe support in this
captured geometry. Stock lighting coefficient count is **zero on all maps**.

Some statically certified local support is unassigned in the incomplete region
graph: hoth3 1,169 / 1,830 probes; vjun2 1,325 / 2,481; mp/duel4 1,126 / 2,225.
These keep explicit RegionUnknown while local certificates may remain valid.
Region IDs do not expand optical coverage. There is no stock room ground truth
against which to report a calibrated room-misclassification rate; it is
Unavailable, not 0%. Controlled wall-side errors are tested by the independent
opaque partition and its admitted-link test above.

Independent exact CPU segment checks over sampled admitted supports report
**1,957 visible paths, zero blocked paths and zero unknown paths** across the
nine ROIs. These validate the prepared box certificate on the sampled set only;
they do not certify untested geometry, doors, sky or physical source existence.
`coverage.csv` records supported/unknown/solid volumes, region assignment,
link/work counts and each ROI; `stock.json` retains all exact counters and hashes.
The initial report's `unknown_volume` is the unsupported remainder and includes
known Outside-world cells as well as unresolved geometry. Individual serialized
cell `SpaceClass` values retain that distinction; it is not a pure missing-geometry
volume measurement.

### Costs, determinism and failed attempts

Stock build/query work, process wall/RSS and encoded bytes are in the versioned
JSON. Each ROI uses roughly 0.65–1.11 MiB of owned topology, with larger shared
map geometry outside this count. Prepared field queries on the small controlled
fixture require about 1–2 microseconds on the measured CPU under concurrent work,
with **zero query rays and no heap allocations in the query implementation**.
The final concurrent process measurements are hoth3 8.797 s / 90.49 MiB RSS,
vjun2 9.516 s / 82.43 MiB, and mp/duel4 1.937 s / 26.13 MiB; these include input
and shared geometry, three regional graphs/topologies, reference checks and
round-trips, not only interpolation.
The repeated controlled processes use 18.25 / 16.71 MiB peak RSS and take
2.078 / 1.875 seconds for the entire 8,274-assertion fixture suite. The measured
100k ordinary query loops are 2.04 / 2.36 microseconds per sample in that frozen
run; an earlier isolated loop was about 0.91 microseconds. These are CPU/time
measurements, not deterministic correctness witnesses or a GPU estimate.
The stock query diagnostic includes domain/reference-ray checks and must not be
reported as pure interpolation throughput. Wall timings vary with other agents.

Probe keys and repeated MP topology are identical. Original v1 failed strict
round-trip because invalid cells' unused first-link index was restored
inconsistently; its logs and protocol are preserved and marked development-only.
v2 repairs that failure. Final v3 adds the non-axis float-direction adapter
regression, directional RGB spatial measurements and incomplete-chunk tests.
No thresholds, geometry margins or rejected opacity rules were loosened to obtain
these passes. SP/MP and final CPU verification are recorded in `checks.json`.

### Final verification and cache limitation

SP and MP Release builds pass. Python report tests pass 3/3. The full final CPU
CTest run is **40/41**, 399.47 seconds under concurrent work: the probe test and
all mathematical/geometry/solver tests pass, but the existing S13 producer cache
test fails two repeated atomic stores at iterations 0 and 18 with Win32 **1175**
(`atomic cache write/flush/replace failed`). The same intermittent repeated-store
test had already failed in the preceding X15 work. P02 does not change the cache
writer or invoke its probe subsystem at these stores.

A single declared follow-up run of the same producer test with a fresh isolated
cache directory passes **549 checks**, without changing assertions, cache logic
or retries. This does not erase the failed full run or establish that the cache
problem is fixed. Its underlying OS/cache-lifecycle cause remains unresolved.
The original/follow-up log hashes are preserved. Therefore the repository-wide
all-green CPU-suite criterion is **not** claimed, despite passing P02-specific
tests, determinism, nine stock geometry scopes and SP/MP builds.

```powershell
build/s06-gcc/lightrecon_producer_test.exe C:/Users/Keyten/Desktop/projects/OpenJK-rend2/build/p02-isolated-producer-cache
```

## Remaining limits

The conservative joining-box certificate can reject a valid bent corridor or a
segment near an aperture even when a centre ray is clear. The fallback local
clearance cubes intentionally leave substantial support holes. A later certified
tetrahedral/portal-cone interpolator could improve coverage; a moment estimate
cannot substitute for that proof. Thin/missing/alpha geometry remains uncertain.
No automatic stock lighting-error refinement, scene-wide streaming controller,
dynamic-door rebake or full illumination bake is implemented. Multiple fields
can coexist as independent prepared instances; there is no automatic union or
composition of exclusive/root-unknown data. No stock illumination accuracy,
absolute photometry or original light recovery is claimed.

## P01 follow-up

[P01](rend2-lighting-field-builder-p01.md) now populates this topology from explicitly
selected canonical source/sky/passive-bounce data and installs a prepared CPU field
backend. Its separate typed conditional API is used for relative/appearance fields.
The Snapshot field entry points require authored physical Lux/Nits; P02's direct
mathematical interpolation utilities still permit relative values. Schema 8
invalidates older asset caches. These changes do not revise the frozen P02 geometry
or resource witnesses, and do not establish any stock illumination truth.
