# P05 — reusable static source visibility and shadow support

## Architecture and scope

P05 adds an offline sparse cache of explicit source/receiver and segment/sky-ray
requests. It uses the existing `StaticVisibility` visual BVH, `RegionDomain`, X13
occupancy constraints, X14 source-support checks and `Shape::EmissionNodes`.
There is no second triangle intersector, per-source BVH, shadow map, camera cascade,
renderer texture format or lighting consumer. Preparing visibility does not raise
source confidence or certify original JA source geometry.

The cache is camera independent and separate from P01 illumination coefficients.
Source records remain unchanged. No baked lightmap, Forward+, CSM, fog, entity
shadowing, dynamic lighting or `r_lightReconShadows` activation is changed.
Stock historical compiler visibility remains unidentified. This cache predicts its
declared physical static scene, **not** Raven or q3map2 compiler-specific boundary
replay. It does not copy the source-locked compiler barycentric epsilon into
general physical geometry.

Implementation: `shared/rd-rend2/lightrecon/shadow_cache.h/.cpp`,
`shadow_cache_codec.cpp`, `shadow_cache_prepared.cpp`, `shadow_cache_internal.h`;
native tests in `tools/lightrecon_test/lightrecon_shadow_cache_test.cpp`.
`Snapshot::QueryShadowSupport` is the new typed diagnostic boundary. Existing
`QueryVisibility` and `QuerySkyVisibility` adapt only strict supported records.

## Four distinct meanings

Each requested item stores two independent pairs of estimates:

1. **Static geometry visibility** over supported source points.
2. **Declared bake-scenario visibility**, narrowed by X13 participating model states.
3. Corresponding **response-weighted** fractions, when emission supports the receiver.
4. Separate runtime-motion limitations; these are never baked into permanent geometry.

PVS, equal/different region IDs and probe distance are not visibility predicates.
Regions are provenance/indexing labels. Source-centre feasibility, emitting-side
support and receiver ray visibility remain separate checks.

For sampled weights `w_j`, known-visible ray set V and unresolved ray set U,

\[
 T_{lower}=\frac{\sum_{j\in V}w_j}{\sum_jw_j},\qquad
 T_{upper}=\frac{\sum_{j\in V\cup U}w_j}{\sum_jw_j}.
\]

An estimate is known only when all positively weighted sampled rays are known.
Unknown records have `state=Unknown`, `estimateKnown=false`, explicit bounds,
and no usable default point value. These are uncertainty intervals for the
**sampled ray set**, not confidence bounds for continuous area-source integration.
The old scalar API refuses unknown/conditional data instead of returning black
or full visibility. A known static opaque obstruction remains blocking despite
unknown motion or earlier uncertain alpha surfaces.

Source-area fractions use emitting surface quadrature area weights. Incident
response weights additionally use source cosine and inverse-square geometry;
when a receiver normal is supplied they also include its cosine. No RGB/intensity
is fitted or multiplied into the cache. These ratios are dimensionless, not lux.
For a spot outside its cone, geometric visibility may be clear while response is
not applicable. Zero angular emission is not an opaque blocker.

## Source and receiver support

| Request | Supported behaviour | Limits |
|---|---|---|
| Arbitrary finite segment | Binary supported static ray, distance to known opaque terminator | Explicit cached endpoints; unknown optics remain unresolved |
| Point | Receiver-to-known-position visibility | Ideal point only; finite point radius unsupported |
| Spot | Point visibility plus canonical cone/mount support | Smooth-cone model, not unknown historical angular formula; range is a culling hint, not a physical cutoff |
| Rectangle | Multi-point one/two-sided Lambertian emitting support | Explicit source-side evidence for mounted/two-sided geometry |
| Open finite tube | Curved-side source quadrature | No invented luminous caps; finite sampling uncertainty |
| Parallel sun | Ray opposite photon travel, requiring an actual sky encounter | Finite solar disk unsupported; extent uncertainty is not solved |
| Diffuse sky | Explicit directional sky rays | No-hit does not prove sky; no inferred sky radiance or hemisphere energy |

The existing visual BVH handles opaque walls, triangles, thin geometry and bounded
patch approximations. Curved/unsupported/alpha/translucent support is propagated
through its actual reliability and issue flags. Numeric material transmittance or
alpha-mask integration is **not available** from the present material API; P05
does not invent it. Explicit AssumeOpaque/Ignore policies remain assumptions,
not measured transmission. Fractional visibility is positively validated here
for finite emitters, not for an unimplemented transmissive shader model.

Receiver source-surface IDs, geometric normals and footprint-derived bias limits
are retained. Existing ray offsets protect self intersection without moving the
canonical observation. Tolerances use existing relative/absolute numerical policy.
There is no universal wall dilation or arbitrary inverse-square singularity clamp.
Singular source/receiver geometry is unsupported locally.

Explicit mounting contracts are checked with X14 against the **static** domain and
visual geometry. This check excludes mover occupancy solely for the static-side
certificate; every actual source/receiver ray still receives the real X13 scenario
constraint. Only certified named mounting contacts can suppress an endpoint hit,
and only at that source endpoint. Unrelated opaque barriers remain blockers.
Recessed and unsupported assemblies do not receive a generic free-space exemption.

## Static, bake-time and runtime motion

`StaticVisibility` excludes captured moving visual surfaces. P05 reuses X13
`OccupancyEvidence`/`BakeState` instead of treating current/captured bounds as a
complete swept trajectory or filled solid box. Selected included states use their
actual prepared geometry; excluded states must be explicitly selected. Competing
open/closed states produce separate scenario datasets, not summed occlusion.

An unbounded mover can make bake/runtime occupancy unresolved, but cannot erase an
independently supported static result. Under an explicitly named ConditionalBake
scenario, distant original model bounds permit an **assumed**, locally scoped
result. This is not proof of a historical pose or empty runtime motion. Without
that opt-in, the scenario remains Unknown while the static half remains queryable.
Known swept bounds disjoint from the segment avoid a runtime-relevance warning;
unknown bounds never become inferred finite bounds. Relevant/unbounded runtime
motion sets `runtimeUpdateRequired` separately.

Legacy scalar adapters cannot express those limitations: they decline uncertain
runtime paths, except where a permanent known opaque static barrier already proves
blockage. Future consumers must combine static support with independently evaluated
dynamic occlusion, source photometry/angular response and receiver shading. P05 does
not claim final moving-door/character shadow correctness.

In particular, multiplying a static **area-averaged** visibility by an independently
area-averaged dynamic visibility is generally wrong: their masks can be correlated.
A future soft-shadow consumer must combine per-source-node masks jointly, or use
explicit bounds/retrace at its supported nodes. P05 stores aggregate estimates,
not the full per-node mask needed for exact dynamic composition. The sampling recipe
is retained separately from its dependency hash; no averaged fraction proves such
composition safe.

## Sampling quality and finite sources

Area/tube results compare the requested quadrature with a denser rule using the
same declared model and current source geometry. The final estimate uses the denser
rule. The maximum difference of geometric/response intervals is retained.
Above the declared 0.02 default threshold, `SamplingDisagreement` remains explicit;
the result is not made exact by solver convergence. If the hard quadrature ceiling
prevents a distinct refined rule, `ResolutionLimited` and zero reference samples
record that no independent refinement estimate exists. Identical rules cannot
claim zero quadrature error.

Finite-source records always retain `FiniteSampling`. An all-clear finite sample
set is not universal proof of a fully visible continuous emitter. A very thin caster
between nodes can still be missed; refinement discrepancy is a diagnostic, not a
rigorous error bound. Independent dense analytic-slab references test this limitation
and the controlled shadow-edge cases.

## Sparse data, identity, reuse and queries

Requests explicitly name stable receiver IDs, positions/normals/surface and region,
source ID or segment/sky direction, and work priority. Stable receiver identity cannot
refer to different geometry within one build. Computational selection is bounded;
the cache does not generate a dense all-source/all-voxel matrix. `ProbeItems` adapts
selected P02 probe geometry under an explicit pair budget, without reading coefficients
or asserting irradiance truth. A valid probe certificate is not source visibility.

Cache identity includes algorithm version, map/BVH/domain generations, optical and
bias policy, source geometry and angular/mount support, scenario and occupancy-policy
identity, receiver geometry and numerical sampling settings. Source photometric RGB
is deliberately excluded: brightness changes cannot alter geometric occlusion.
Completed exact dependency matches can be reused; changed XYZ/shape invalidates only
its dependent records. Changed geometry/optical/scenario policy invalidates affected
scope. Failed or incomplete artifacts are not accepted as completed cache inputs.

Records are sorted deterministically and retain known/unknown/conditional/unsupported
counts. Source and region are queryable metadata, not hard visibility partitions.
Prepared source/region coverage indices provide bounded counts without scanning
the entire dataset; their existence is not a room-connectivity proof.
Public typed queries require dataset, stable receiver ID, source handle, scenario,
and matching world position/direction. Missing support returns Unavailable. A wrong
configuration is NotFound/Ambiguous; stale handles/map/source generations reject.
No interpolation of visibility is performed. Multiple different receiver records
at the same legacy coordinate query must not be arbitrarily chosen.

Preparation creates immutable ID and coordinate indices before Snapshot publication.
Queries perform bounded binary lookup and fixed-size result copies: no new rays,
disk access, lazy caches, reconstruction, shaders, GL, or per-query allocation.
The legacy APIs support explicit finite segment and sky records, plus a matching
parallel-sun sky request. No cache miss falls through to uncontrolled global tracing.
One prepared cache backend currently serves one scoped dataset/scenario. A future
composite backend must preserve explicit alternatives rather than choose a best
stock profile or sum incompatible scenarios.

## Persistence and budgets

Reuse canonical `VisibilityReference` and `DatasetPurpose::Visibility`.
`org.openjk.static-shadow-support/1` stores a descriptor chunk followed by at most
64 sparse records per chunk. Existing canonical asset schema **8 is unchanged**;
the new payload has its own version. Header/record checksums, dataset content hash,
dependency keys, actual data content identity, source geometry, counts, sorted IDs,
finite intervals and expected map/geometry are validated at explicit load time.
Absent records mean unknown, not zero.

Float64 is the reference scalar storage. Optional UNorm16 rounds values and widens
interval endpoints outward. The additional per-endpoint quantization error is at
most `1/65535`; this bound is separate from quadrature and geometry uncertainty.
Repeated encoding must not repeatedly widen already quantized bounds. Occluder
distance and world coordinates are not quantized into a shadow-texture format.

Default batch caps: 100k records, 4M static rays, 128M counted geometry/work units,
128 MiB logical allocation budgets and 300s wall safety. At most 4096 distinct source
geometries are handled in one bounded build batch; this is not a renderer or map-wide
physical source-count limit. Shared BVHs are never cloned per source. Ray stacks
remain bounded by the existing intersector. Input/source quadrature/metadata/chunk
limits and actual OS RSS are reported separately; logical value-byte counters do not
include the externally leased static BVH or every allocator/copy overhead.

Cancellation and exhausted mandatory work/ray/memory budgets discard the incomplete
artifact. Locally unsupported source/receiver geometry is an explicit Unknown record,
so available independent records remain usable. No unsupported observation is
counted as successful darkness or source absence. This cache covers explicit
segments, not a claimed continuous volume.

## Reproduction and measured results

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc -j 2
& C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -X utf8 tools/rend2/shadow_cache/run.py --out tools/rend2/shadow_cache/results/v3
ctest --test-dir build/s06-gcc --output-on-failure -j 2
$env:CL='/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

The runner refuses to overwrite results. It freezes code/executable/compiler/settings
before running the native benchmark twice, retains semantic and byte determinism,
and separates CPU/time/RSS from numerical witnesses. References are independent
analytic ray/slab intersection plus dense source quadrature, not another copy of the
cache's ray loop. Python launches and measures C++; it does not compute predictions.
No licensed geometry, new numerical dependency or proprietary payload is required.
New code uses GPL-2.0-or-later, with C++ and Python standard libraries only.

## Frozen result

[Protocol](../tools/rend2/shadow_cache/results/v3/protocol.json),
[native results](../tools/rend2/shadow_cache/results/v3/summary.json),
[CSV](../tools/rend2/shadow_cache/results/v3/quality.csv),
[analytic panel reference](../tools/rend2/shadow_cache/results/v3/analytic-reference.json).
The native numerical witness passes **262 checks**, twice. Numerical semantics and
complete canonical archive bytes match. The owned example archive SHA256 is
`7d6e5516be54b789c50abaf4f808bf8b8006fc6050709e2ea393d6c6896f4573`.

| Controlled check | Result | Interpretation |
|---|---:|---|
| Known opaque thin-wall false-clear | **0/64** | All requested samples retained |
| False occlusion in primary open controls | **0/3** | Three clear source/segment records asserted visible |
| Doorway | 1 positive + 1 blocked control | Opening transmits; region/cluster is not an occlusion test |
| Source-to-probe | **1184 records: 592 visible, 592 blocked** | Geometry-only P02 probes; no irradiance invented |
| Unknown alpha/translucent | **2/2 remain Unknown** | Neither transparent nor opaque by default |
| Area response visibility | Cache 0.408740; analytic **0.3989979222** | Absolute error **0.0097421**, or **0.9742 percentage points** |
| Tube response visibility | Cache 0.558671; dense 0.553533 | Absolute error **0.00513793**, or **0.5138 percentage points** |
| Dense-reference 512→1024 difference | Area 5.43e-9; tube 0.000254585 | Resolution diagnostic, not universal error bound |
| Source quadrature coarse→refined difference | Area 5.62e-6; tube 0.0392994 | Tube exceeds default 0.02 diagnostic threshold |
| Float64 / compact single-row payload | **980 / 908 bytes** | Descriptor + sparse row + checksums |
| Query allocations/rays | **0 / 0** | 100,000 repeated prepared lookups |
| Warm reuse / changed brightness | **3/3 reused; zero rays** | RGB-independent geometric support |
| One changed XYZ | **1 rebuilt, 2 reused; one ray** | Source-specific invalidation |

For the panel, an additional closed-form solid-angle integral validates the
finite-thickness slab reference. The source is fully separated from the wall and
receives an X14 source-side certificate. The visible subrectangle starts at
x=2.01999999955; integrating `h/r^3` over the visible and complete panel gives the
ratio above. The dense 512 reference differs from the analytic value by 0.0002271.
Therefore the small **difference between two cache quadrature rules is not its
actual error**: regular rules can alias the same boundary. P05 retains
`FiniteSampling`, never a fictitious narrow confidence bound. Neither finite-source
record is admitted through the strict legacy scalar adapter.

All 1184 source/probe outcomes are known **at their sampled rays**. This does not
certify a continuous volume, probe lighting, every receiver normal, or stock source
existence. Direct-source and interpolation-support tests are separate.

| Selected point-source batch | Rays | Estimated record bytes | Build time across repeats |
|---:|---:|---:|---:|
| 1 source × 32 receivers | 32 | 13,312 | 1.32–1.41 ms |
| 8 × 32 | 256 | 106,496 | 10.75–12.17 ms |
| 32 × 32 | 1024 | 425,984 | 44.66–45.56 ms |

The area cache uses 1280 rays and 2.69–2.83ms; tube uses 1216 rays. Logical resident
record cost is 416 bytes per row on this build; descriptors, metadata, preparation
overhead and externally shared geometry are additional. Whole native-suite peak
RSS is **10.64–12.66 MiB**, process CPU **0.844–0.875s**, wall **0.893–2.164s**, under
concurrent machine load.

Prepared lookup measures **37.6–41.6ns**, versus **516.7–626.2ns** for the same
repeated direct ray on the empty-geometry control. This excludes publication,
construction, disk loading and Snapshot validation. It is not a stock-BSP throughput
or GPU claim. Compact storage saves 72 bytes per row; metadata limits the percentage
saving in tiny datasets. Its `1/65535` bound concerns quantization only.

## Verification history and limits

v1 is preserved as **superseded preliminary evidence**, not physical acceptance:
its finite-source fixture intersected its occluder and its reference used the slab
centre plane instead of full thickness. v2 repairs the fixture, requires source-side
certification and checks 1024 sampling resolution. v3 also rejects dangling provenance
and malformed source-support metadata. Acceptance thresholds were not relaxed.
v2/v3 predictions and example archive bytes agree; validation assertion counts differ.

The complete CPU suite passes **43/43**, including Producer/Joint, uncertainty,
source refinement, P01/P02, visibility/lifecycle and disabled-renderer contracts.
That complete run preceded the test-only finite-fixture correction. A subsequent
**41/41** run rechecks the corrected fixture and the suite except unchanged expensive
Producer and point-refinement tests, already passed above. Final v3 guards pass a
fresh **9/9** focused regression. SP and MP Release builds pass on final production
sources. Exact logs are retained in `checks.json`. Existing x64 `/arch:SSE2` warnings
are unrelated. No game was launched. Previously reported Win32 1175 did not recur;
P05 does not modify or claim to fix that cache writer. Build RSS wrappers measure
the CMake parent, not aggregate compiler memory.

Tests include cancellation before/during work, ray/memory caps, unsupported solar
extent/unknown source geometry, malformed support/provenance, missing/corrupt chunks,
changed XYZ, wrong map, retained/stale snapshots, wall-side receivers, flush one-sided
panels and independent blockers, uncertain optics, curved/unbounded geometry,
excluded movers, distant unknown trajectories, bounded unrelated motion, open/closed
states, coordinate ambiguity, sampling ceilings and repeat encoding. Missing data
never counts as successful darkness.

Remaining limits: numeric alpha/translucent transmission, continuous-source error
proof, automatic importance controller, per-node mask persistence/dynamic composition,
receiver-footprint integration, finite solar disk, out-of-core building and stock
physical shadow restoration are unsupported. Incident/diffuse response fractions are
not universal GGX or volumetric shadow factors; those consumers need their own jointly
weighted sampling. Original visual geometry supplies optical blockers; hypothetical
emission geometry does not create an unrelated opaque mesh. Historical FAST/cutoff
behaviour remains in the bake operator: RGB-free visibility reuse does not make its
intensity response globally linear.
