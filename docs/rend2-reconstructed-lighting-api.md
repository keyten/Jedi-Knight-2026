# Reconstructed lighting CPU access API

`shared/rd-rend2/tr_lightrecon_api.h/.cpp` is the read-only C++11 access boundary
for the [canonical lighting asset](rend2-reconstructed-lighting.md). It includes
no world/BSP, dynamic-light submission, shader or OpenGL types. Its only runtime
dependencies are the C++ standard library and canonical CPU type validation.
It is built in the shared [CPU core target](rend2-reconstruction-core.md), along
with the streaming input and producer-side session boundary.

`tr_lightrecon_manager.h` is the producer/owner interface. Consumers need only
the API header. Querying a snapshot never starts a reconstruction, opens a
dataset locator, allocates graphics resources or changes renderer state.

## Public read interfaces

| Function | Result |
| --- | --- |
| `Snapshot::State`, `Generation`, `Message` | Availability, publication identity and owner diagnostic |
| `GetCapability` | Explicit metadata presence or CPU evaluation support |
| `Asset` | Pinned const canonical asset, including schema, lineage, units, provenance, calibration and dataset descriptors |
| `Emitters`, `EmitterCursor::Next` | Lazy filtered enumeration of pinned const physical-emitter hypotheses |
| `Sun` | Unique resolved sun plus receiver-to-sun unit direction |
| `Sky` | Unique resolved environment metadata |
| `Environments`, `TransportFields`, `Regions` | Pinned const metadata views |
| `VisibilityData`, `Contributions`, `Diagnostics` | Pinned const metadata views, including source lists and evidence limitations |
| `IdentifySource`, `IdentifyRegion` | Convert a persistent nonzero ID to a publication-bound handle |
| `GetSource`, `GetEmitter`, `GetRegion` | Indexed ID lookup with generation checking |
| `Quality` | Const source evidence and optional emitter record containing position/intensity uncertainty |
| `SampleIndirect` | Prepared indirect irradiance for a world point and unit surface normal |
| `SampleIncomingRadiance` | Prepared incident radiance for a point and receiver-to-source unit direction |
| `LookupRegion` | Exact prepared geometry-region classification; no AABB-as-region fallback |
| `QueryVisibility` | Prepared scalar transmittance along an explicit finite receiver-to-target segment |
| `QuerySkyVisibility` | Prepared scalar transmittance along a receiver-to-sky unit ray |
| `SampleContribution` | Per-source contribution at a stable sample ID in a declared sample domain |
| `Weak`, `WeakSnapshot::Lock` | Non-owning snapshot ticket and explicit `Expired` result |

Each operation that may fail returns `Result<T>`. Only `status == Ok` makes
`value` a result. Failure returns no pinned record and a default-initialized
non-result value; zero RGB/transmittance there is **not** a lighting measurement.
Test the status, or the result's explicit boolean conversion, before reading it.
Provider output is discarded on failure, even if the provider wrote a value.

`EmitterFilter` can select exact emitter kind (including future extension kinds),
hypothesis states, region membership, exact-light candidate suitability, sun role,
and minimum physical/transport confidence independently. Its default includes
candidate/resolved records and excludes rejected hypotheses. A confidence filter
of `-1` is disabled; `[0,1]` requires a known central confidence meeting that
threshold. Unknown confidence is not zero and does not pass an enabled threshold.
Filtering never rewrites geometry, intensity, confidence or hypothesis selection.

`Sun()` uses the explicit sun-role references, not every directional emitter.
No resolved sun yields `Unavailable`; multiple independent resolved suns yield
`Ambiguous`. A resolved sun without a known frame is also `Unavailable`. It does
not choose the first candidate or use the renderer's default sun. Sun intensity
and colour may still be explicitly unknown in the returned canonical record.
To inspect all alternatives, enumerate with `sunOnly=true` and desired state bits.
`Sky()` similarly refuses to choose between unresolved/ambiguous environments;
`Environments()` exposes every candidate for inspection.

## Availability and capability semantics

| Owner state | Read behaviour |
| --- | --- |
| `Disabled` | `Disabled`: canonical reconstruction deliberately disabled by its owner |
| `Missing` | `Missing`: no canonical result is installed; a default snapshot is also Missing |
| `Pending` | `Pending`: producer/loader is still preparing a result, reads never wait or start it |
| `Ready` | Read validated published data; individual capabilities may still be absent |
| `Partial` | Same safe reads, explicitly incomplete reconstruction; no capability is fabricated |
| `Corrupt` | `Corrupt`: validation or explicit producer load failure; no current asset exposed |
| `Unloaded` | `Unloaded`: owner cleared the world/result; no current asset exposed |

`Ready` versus `Partial` is declared by the producer, not inferred from the number
of lights. Publishing an empty asset is legal: asset metadata can be read,
enumeration succeeds with zero records and sun/lighting queries do not invent
an answer. Invalid publication replaces the current result with a Corrupt state
and validation diagnostic, while already pinned historical snapshots remain safe.

Metadata capabilities are `AssetMetadata`, `PhysicalEmitters`, `SunEstimate`,
`SkyEstimate`, `IndirectField`, `IncomingRadianceField`, `GeometryRegions`,
`VisibilityData` and `SourceContributions`. `CapabilityInfo::records` counts
metadata records, including incomplete/rejected hypotheses. `Ok` means those
records exist, not that an external dataset is resident, a source is resolved or
an unknown intensity has been estimated. Zero relevant records yields
`Unavailable`. Metadata enumeration on a valid empty asset still returns an empty
successful view/cursor, making iteration safe without inventing records.

CPU evaluation capabilities are separate: `IndirectSampling`,
`IncomingRadianceSampling`, `RegionLookup`, `VisibilityQueries`,
`SkyVisibilityQueries`, `ContributionSampling`. They require relevant metadata
and an explicitly installed prepared CPU evaluator. Capability declarations are
cached at publication, not probed per frame. Region connectivity/PVS metadata
alone does not enable segment or sky shadow queries.
Evaluation capability records counts are unused (zero); inspect their status.

Additional result statuses:

- `Unavailable`: data, resolution or an evaluator is not currently available.
- `Unsupported`: an installed evaluator explicitly cannot handle the relevant
  capability/encoding. Unsupported emitter metadata can still be enumerated.
- `InvalidArgument`: malformed enum/filter/ID or invalid query geometry; vectors
  are not silently normalized. Basic arguments are checked before evaluation.
- `NotFound`: a requested persistent ID is absent, cursor is exhausted, an array
  index is out of range, or a prepared evaluator reports no sample/domain match.
- `Ambiguous`: no unique resolved answer, or overlapping contribution references
  match the same request. The API never silently adds/selects them.
- `StaleHandle`: a valid-looking handle belongs to another publication/manager.
- `Expired`: a weak snapshot no longer has any strong lease.
- `Corrupt` from evaluation: malformed prepared values/IDs/status returned by an
  evaluator. This call fails; the immutable snapshot itself is not mutated.

No failure status falls back to the legacy grid or renderer's sun. Consumers may
choose their own explicit fallback outside this API after examining availability.

## Query coordinates and photometry

Positions/endpoints are finite JA world coordinates, including negative values.
Normals and directions are unit world vectors with `kUnitTolerance` from the
canonical contract. Zero, non-finite or non-unit vectors are invalid. The API
does not clamp positions to a field or transform them to BSP/froxel coordinates.

An irradiance normal points out of the receiving surface. Incoming radiance and
sky visibility directions point **from the receiver towards the source/sky**,
opposite photon propagation. A canonical spot/directional frame's forward axis
is photon propagation. `Sun().directionToSun` is precisely its negative, and is
not copied from `tr.sunDirection`. Segment visibility uses the explicit receiver
and target endpoints; coincident endpoints are invalid. An optional source handle
only identifies the relevant prepared dataset, never supplies a guessed endpoint.
An explicit sky visibility source must be a directional emitter or environment.

`LightingSample::rgb` is nonnegative linear sRGB/Rec.709 D65. Its Rec.709 Y is in
lux for irradiance, nits for radiance, or explicitly `Relative`. Each result has
calibration provenance. No `physicalToScene`, exposure, display encoding or fog
scale has been applied. The physical/legacy-equivalent/relative distinction from
the canonical asset remains in force; relative results must not be relabelled
physical. Consumers wanting scene-scaled values apply the declared fixed scene
scale once at their own rendering boundary for calibrated quantities.

An explicit field handle selects an indirect-irradiance transport record for
`SampleIndirect`, or a radiance transport/environment record for incoming
radiance. Other source types are invalid for those queries. Both zero handle
fields request the evaluator's automatic selection. Automatic selection must
respect resolved sources and report ambiguous/unavailable hypotheses, not infer
a sun or select the first field. Explicit handles may select a candidate for
inspection. Successful values name the selected source ID and must match that
field's unit/calibration. The core also verifies that automatic results name a
resolved field/environment of the correct quantity. It rejects an evaluator
that reports success with an unknown source or silently selects a candidate.

Visibility is scalar `[0,1]` transmittance for the segment or sky ray, not a
shadow-map lookup. Contributions use `(sampleDomain dataset ID, stable sample ID,
source handle, component, quantity)`. Sample IDs are not dense array indexes.
The matching canonical contribution reference governs units and source scope.
Dimensionless fractions lie in `[0,1]`. Missing sparse entries remain unknown;
a future evaluator reports `Unavailable`/`NotFound`, never an implicit zero.

The API validates successful evaluator outputs for finite/nonnegative RGB,
quantity/calibration agreement, valid region IDs, visibility/fraction ranges and
allowed query-result statuses. It does not certify the accuracy of an estimator.

## Ownership, publication and identities

`Manager::Publish` copies the small canonical asset metadata once, validates it,
builds lookup/capability indexes and atomically publishes an immutable snapshot.
Moving a builder vector into a const pointer alone would leave possible writable
aliases; the deliberate copy avoids that. No lighting volume is copied: canonical
large datasets are descriptors. Invalid assets are never partially published.
Mutation/construction stay in the manager and future builder/evaluator code.

`Acquire` and snapshot reads can run concurrently with publication. One owner
serializes manager writes and keeps its input stable while publishing. The
published asset and CPU evaluator must remain immutable. Optional evaluators
are owned through `shared_ptr<const CpuEvaluator>`; their producer must freeze
any externally owned backing storage too. Memory-allocation failures during
publication may throw the standard allocation exception; queries do not perform
the metadata copy/index build. There is no loader, job scheduler or waiting path.

Snapshots, emitter cursors, arrays and individual record leases pin the same
publication. Copies only retain a shared ownership pointer, not a vector/volume.
Previously acquired leases remain readable after a rebuild, clear, unload or
manager destruction. An old snapshot consistently answers from its own revision.
A weak ticket alone pins nothing; `Lock` safely returns `Expired` after the last
strong lease is released. Iterators/pointers/references borrowed from a view need
one strong lease to remain alive; `ConstArray::At` returns its own pinned record.
`Message()` is similarly borrowed from its snapshot. As with other renderer CPU
objects, all leases must be released before the renderer DLL/code is unloaded.

Persistent source/region IDs retain their canonical lineage semantics. Publication
generations are unique across managers during this API module's lifetime and
never reused/wrapped. They are not serialized and must not survive DLL reload.
`IdentifySource/Region` binds an ID to that generation; another snapshot rejects
it with `StaleHandle`, even if the new asset happens to reuse the same numeric ID.
After a rebuild, acquire the new snapshot, compare `Asset()->lineage`, and resolve
IDs explicitly. A removed/renumbered ID is `NotFound`; no implicit remapping exists.
Caches should key by lineage, generation and ID, or retain a historical snapshot
deliberately. Reset cached handles on module unload.

## World lifecycle and unchanged rendering

The renderer adapter is `R_ReconstructedLighting()` in `tr_lightrecon.h/.cpp`
(also included by `tr_staticlighting.h`).
It acquires a CPU snapshot without accessing `world_t` from the core API. The
manager is a normal C++ owner beside the existing main-world static-lighting
owner, outside `world_t`, which is hunk-allocated and zeroed with `Com_Memset`.
No `shared_ptr` or nontrivial container is inserted into that world structure.

SP/MP renderer reset/shutdown marks the canonical owner Unloaded, independently
of `R_ClearStaticLighting`. At the main-world `RE_LoadWorldMap` boundary, the
[lifecycle manager](rend2-reconstruction-lifecycle.md) applies the independent
[build/load policy](rend2-reconstruction-controls.md): default policy 0 exposes
Disabled. Complete production source fingerprints/input export remain unavailable,
so enabled policies expose Missing with explicit NotCacheable/BuildRequired
diagnostics instead of accepting an underspecified cache key.
The experimental grid reconstruction still
builds its existing result. Neither its proxies nor its promoted fog lights are
presented as canonical physical lights. Sub-BSPs do not replace the main-world
owner. A future producer can use the internal manager to set Disabled/Pending,
publish Ready/Partial, or report missing/corrupt input.

`r_staticLightReconstruction` controls only the existing experimental path; it
does not define canonical availability or depend on a future consumer's enable
flag. The new `r_lightRecon*` controls apply only to the canonical owner and future
consumers; no renderer light-budget policy is introduced. Existing
`R_StaticLights()`/`R_StaticLightPortals()` accessors, sun/entity lighting and area
light calibration continue their prior behaviour. Fog, `tr_light.cpp` and
`tr_arealights.cpp` are not migrated to the new data or shaders by this task.

## Intentionally unavailable and verification

No production indirect/radiance sampler, region classifier, segment/sky visibility
evaluator or contribution decoder is installed. Those query methods therefore
return `Unavailable` for valid requests. `CpuEvaluator` is an internal boundary
for immutable, already prepared CPU data; its default methods are explicitly
Unavailable. Implementations must not do disk access, lazy mutation, reconstruction,
GPU work or renderer submission from queries. A future decoder can populate this
boundary without changing consumer query types or coupling the API to storage.

The `lightrecon_api_test` program uses synthetic manually populated assets and
test-only prepared values. It covers states, empty assets, all emitter kinds,
filtering, read-only types, independent uncertainties, resolved/ambiguous sun/sky,
direction conventions, invalid queries, corrupt publication/provider output,
generation changes and strong/weak lifetime after unload. Repeated query loops
check stable pointers, unchanged publication and zero heap allocations. The CPU
target links only the API/types and standard library; unreachable dataset locators
remain unread. Test-only supplied values are not production fallbacks.

Build with `BuildLightReconTests=ON` or configure `tools/lightrecon_test` alone.
CTest runs `lightrecon_contract`, `lightrecon_api` and `lightrecon_core`. Both SP/MP
Rend2 targets link the one CPU archive; runtime lighting still uses the existing paths.
