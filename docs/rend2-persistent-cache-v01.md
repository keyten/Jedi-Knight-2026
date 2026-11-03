# V01: persistent reconstructed-lighting CPU cache

The existing `.jklr` envelope is retained (file 1.0, canonical schema 8).
The spatial-field payload advances to **2**, adding a frozen source-scene digest:
source movement, photometry/calibration or configuration changes reject stale
coefficients even if someone accidentally pairs them with new asset metadata.
Old field payloads are Unsupported and require derived-field rebuilding. Probe
geometry/topology and static-shadow encodings remain independently versioned at 1.
There is no second light-cache format, renderer manager, GPU texture format or
lighting consumer. V01 adds dependency collection, independently keyed derived
layers, selective resident CPU preparation and early map-load lookup.

Three results must remain distinct:

| Layer | Implementation and admission |
| --- | --- |
| Offline numerical result | Existing S13 `Producer::Store/Load`, complete source/configuration/assessment metadata and payload validation. Cache hits do not fit again. |
| Prepared CPU data | `Disk::Persistent::Load/Store`, existing P01 field, P02 probe, P05 shadow and P06 attribution codecs. Only explicitly selected, fully prepared backends are published. |
| Production map load | `WorldLighting::Preflight` and `RendererWorldSource::Describe` collect real VFS content before observation derivation. A pending hit publishes only after successful world initialization. A renderer cache miss still has **no installed automatic inverse-solver adapter**. |
| Future GPU resources | Outside this cache and outside V01. Exposure, Forward+ capacities, froxels, shadow textures and driver handles are absent. |

Loading a cached conditional result preserves its conditional assessment. It
does not certify original Raven emitters or absolute SI calibration. Native
prepared products use `Completeness::Partial`; physical sampling/exact-source
admission retain the existing stricter semantic tests.

## Dependency identities

`Dependencies::Collect` reads the original RBSP1 inventory and hashes content.
It does not derive luxel footprints, prepare a BVH, trace visibility or run an
optimizer. The renderer resolves files through its actual VFS, and resolves
effective shader definitions through the already selected shader-text table.
No shaders or images are registered for this operation.

The complete root fingerprint includes:

- Entire original BSP bytes: geometry/model ownership, entities/source metadata,
  embedded lightmaps, grid records/indexes, styles and deluxe alternatives.
- Lightmap HDR and JPG/PNG/TGA alternatives, deluxe alternatives, grid HDR,
  vertex HDR, area-light source metadata, with explicit **absent-file records**.
  Sidecar names match the engine's basename rule, including MP subdirectories.
- Referenced material definitions, implicit-material status and source
  colour/alpha/emission image alternatives. Images are hashed without decoding.
- Explicit `rend2-sp/source-materials/1` versus `rend2-mp/source-materials/1`.
  An MP BSP captured under SP interpretation is still SP context.
- Decoder, source-geometry/occupancy, supported profile-hypothesis, solver/import
  recipe, numerical settings and calibration-policy identities; exact unit scales.

Bounded unsupported recipes return `NotCacheable`, not a complete guessed key.
These include unresolved world shader remaps, compiler-dependent linked material
definitions and video images. I/O errors differ from absent files. The collector
caps BSP/file/aggregate bytes, file count, material count and lightmap inventory.
Completeness attests this declared source-definition recipe, **not** equivalence
to every runtime shader transformation or the unknown historical Raven bake.
Existing offline artifacts are not automatically rekeyed into that context.

Every numerical producer must version its complete algorithm/settings recipe.
The renderer's current recipe explicitly says `renderer-offline-import/1` and
`no-automatic-solver`; it is not an assertion that a map-load solver exists.
`r_lightReconInfo dependencies` exports hashes/units/context identities for audit,
without map geometry or observed target colours.

`MakeKey` retains canonical input ordering and complete-category checks. Missing
identity cannot become a cache hit. File modification times are not used.

## Independently reusable derived layers

`Dependencies::Derive` produces another ordinary `.jklr` fingerprint. It checks
the complete source fingerprint, units/material context and named required hashes:

| Layer | Required dependency hashes | Consequences |
| --- | --- | --- |
| Probe geometry/topology | `geometry`, `optics` | Independent of emitter amplitude and angular codec. Raw BSP identity is conservatively retained. |
| Static visibility | `geometry`, `optics`, `source-geometry`, `scenario` | Source movement/extent/mounting and occupancy policy invalidate; RGB amplitude need not. |
| Spatial field | `canonical-result`, `probes` | Source photometry/root membership and topology invalidate; SH/SG/projection settings belong to this layer's recipe. |
| Contributions | `canonical-result`, `observations` | Frozen configuration/domain/source changes invalidate. |
| Compression | `uncompressed` | Quantization/codec/version changes do not rerun inverse fitting. |

Effective material contents are also inherited conservatively, not only their
SP/MP context name. Callers supply additional dependencies used by their algorithm. Required hashes
are not optional placeholders; the helper rejects missing/duplicate/zero hashes.
`MetadataDigest` can identify a frozen canonical result; P05 `SourceKey` already
identifies source geometry independently of intensity. These are owner-side
operations. A derived key does not cause query-time reconstruction or automatic
layer discovery. Canonical and derived files may duplicate small metadata.
Raw-BSP changes currently conservatively invalidate geometry-only layers even
when only an embedded lighting lump changed; avoiding that is future optimization.

## Read, prepare, publish

`Persistent::Selection` explicitly selects field IDs, visibility dataset IDs and/or
the attribution view. Field closure includes its two probe support datasets.
`LoadProbes` supports geometry-only reuse without advertising lighting energy.
The S13 attribution closure retains its sample domains, contributions, diagnostics
and supported passive records. P06 validation leaves unrelated P01/P02/P05
payloads to their own numerical decoders rather than treating them as S13 data.

Open validates schema, conventions, expected fingerprint, directory, allocation
bounds and canonical cross-references. Selected chunks then undergo section
checksums and existing numerical/representation/content/source-generation
validation. Geometry generations are expected caller dependencies, not guessed
from a current source's RGB. Unknown codecs, missing records, corrupt data and
stale source geometry fail the selected preparation.

Unselected optional chunks are deliberately **not** called numerically validated.
A corrupt unselected field need not disable an independently validated shadow
dataset. It cannot expose a field sampling capability. All selected backends bind
to the same complete canonical metadata digest. Multiple ambiguous fields/scenarios
require explicit IDs; no lowest-error alternative is silently selected or summed.

No file reader survives `Persistent::Load`. Immutable evaluators contain resident
CPU data. Sampling performs no file I/O, decode, lazy cache fill, source fitting,
global tracing or GPU allocation. Missing sparse support retains Unavailable.

Disk limits bound metadata, chunk bytes, records, allocations and decoded storage.
Preparation also reserves conservatively for simultaneous payload/decoder copies
and per-backend metadata copies. Reservation is a logical admission bound, not a
claim about exact allocator RSS. Individual decoders retain their own limits.
Partial invalidation is explicit at the layer level; automatic per-chunk rebuild
and global layer garbage collection are not implemented.

## Lifecycle and policy

`Preflight(ticket, source, cache)` may describe and validate a cache before costly
observation capture. It never calls `WorldSource::Prepare` and never publishes.
`Complete` consumes that exact pending product once, after world-ready checks.
Failure/abort/unload discards pending products. Old explicitly retained snapshots
remain immutable; no dataset can bind to the new map's asset or generation.

| Policy | Work |
| --- | --- |
| 0 / Disabled | No Describe, dependency reads/hashes, cache reads/writes or solver. |
| 1 / CacheOnly | Early content manifest + validated lookup; a miss never builds/captures. |
| 2 / CacheOrBuild | Same lookup; only a miss reaches explicit source preparation. The current renderer reports missing solver adapter. |
| 3 / ForceRebuild | Complete dependencies, deliberate bypass of cache lookup; explicit build attempt. |

Consumer toggles do not request extra preparation or change build policy.
Renderer warm hits disable reconstruction-only observation capture. Ordinary
world geometry/lightmap rendering uploads remain unchanged. No source is promoted
into Forward+, fog, entities, CSM or world diffuse.

## Native atomic writes and diagnosis

The writer exclusively creates a PID/serial temporary in the target directory,
writes and flushes it, then atomically changes its name. It never deletes the valid
destination to make replacement succeed. Failed writes leave the in-memory asset
and previous cache usable. Crash-orphan `.tmp-*` files are not cache hits.

Windows now uses `SetFileInformationByHandle(FileRenameInfoEx)` with replacement
and POSIX semantics, retaining existing readers' old file identities. Write access
is dropped before the rename; otherwise new readers can fail in the rename/close
interval. The filename is normalized and NUL-terminated as required by the Win32
wrapper, with byte length excluding the terminator. Unsupported older APIs/filesystems
use `FileRenameInfo`; a held destination lease may then honestly cause IoError.
No retry loop conceals that failure. Errors name the failing stage and Win32 code.

POSIX retains write/fsync/rename/parent-fsync. Windows flushes content before rename;
no universal power-loss durability guarantee is claimed for every filesystem.
Controlled precommit interruption hooks are owner/tool-only. Observer exceptions
are contained and cannot strand an open temporary handle.

The historical `ReplaceFile` operation was tested independently. In the second
controlled 500-operation run: retained reader **2×1175**, concurrent readers
0 failures, four concurrent writers **309×1175, 4×1176, 10×1177, 3×32, 3×2**.
Only 171/500 concurrent writes succeeded. Read-only destination returned 5 and
preserved old bytes. An earlier identical reader run had no failure, confirming
that a short streak of success is inadequate. This reproduces the operation's
failure class, not the unknowable exact external state during a historical audit.

References: [ReplaceFile failure semantics](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilea),
[FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info),
[rename POSIX semantics](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/4217551b-d2c0-42cb-9dc1-69a716cf6d0c).

## Validation and reproduction

Measured on Intel Core i7-10750H / 12 logical processors, Windows 10 19045.
Timings are single-run observations, not guarantees; concurrent repository work
and OS filesystem cache affect them. Timing/RSS are outside byte-stable witnesses.

| Controlled native workload | Result |
| --- | --- |
| Cold source-conditioned field + shadow build | 0.177 / 0.179 s; 1,024 field rays + 1 shadow ray |
| Complete file / selected payload | 741,312 / 735,365 bytes; 19 chunks |
| Field coefficients / probe geometry / topology | 216,023 / 136,320 / 382,080 bytes |
| Static visibility only | 942 payload bytes |
| Warm metadata validation | 0.199 / 0.203 ms |
| Warm selected numerical preparation | 13.72 / 12.72 ms |
| Warm inverse/trace work | 0 fitting steps, 0 rays |
| Warm allocator calls / cumulative requested bytes | 11,022 / 4,843,232 bytes |
| Logical resident+scratch reservation | 5,953,704 bytes |
| Peak process working set (whole test, including corruption/concurrency) | 21.29 / 21.86 MB |
| Repeated read-only field queries | 1,000; zero allocations, no file reader retained |
| Real offline Producer cold fit+store / cache-only load | 27.99 / 8.82 ms; 25,280-byte file; 13,362-byte attribution payload; 0 warm validation callbacks/forward calls |

Ten warmed-cache loads per explicit selection (means):

| Selection | Bytes read / chunks | Open / prepare | Release |
| --- | --- | --- | --- |
| Metadata | 5,909 / 0 | 0.191 / 0.163 ms | 2.1 µs |
| Field + required probe support | 740,332 / 17 | 0.223 / 13.372 ms | 122.2 µs |
| Visibility only | 6,851 / 2 | 0.303 / 0.562 ms | 7.2 µs |
| Both | 741,274 / 19 | 0.346 / 20.518 ms | 380.9 µs |

100,000 prepared field queries averaged **5.61 µs** each. This is a CPU fixture
latency, not a GPU shader or large stock-map guarantee. Explicit unload releases
the resident backend; caller-held immutable leases intentionally extend ownership.

Two independent builds produced byte-identical file SHA-256
`40f934f92453b42d45a84b840948ecd6137fea3f5e9a5f7ac0515222cf5ce2d7`.
Both passed 154 native checks. Each includes 200 repeated replacements with an
old reader lease, 4×30 concurrent writer/read iterations, interrupted creates,
writes and flushes, denied replacement, corruption/selective loading, source
movement, derived invalidation and lifecycle checks. A child killed with `_Exit(23)`
after flush left the previous file readable; its orphan temporary was not loaded.
The final full CPU suite passed **44/44** (247.45 s), including field dependency
hardening; subsequent source-image-only dependency narrowing passed 154 native
checks twice. All six affected cache/field/lifecycle tests and 551 Producer checks
also passed.
SP and MP Release renderer builds passed. Existing unrelated `/arch:SSE2` and
missing git-tag build warnings were not suppressed or changed.

The actual **hoth3 SP-context** cache-I/O run measured:

| Case | Dependency hashing | Cache validation/preparation | Process elapsed / peak WS | First valid publication |
| --- | --- | --- | --- | --- |
| Disabled | 0 files, 0 bytes | no cache call | 12.52 s / 504.6 MB | none (Disabled) |
| Cache-only cold miss | 560 paths, 485 explicit absences, 30,723,030 content bytes; 210.31 ms | Missing, no build | 8.85 s / 473.7 MB | none (Unavailable) |
| Warm cache-only | same source identity; 220.64 ms | 53,337 bytes in 6 reads; 2.047 ms Open + 2.736 ms prepare | 9.66 s / 474.7 MB | 1.147 s after Begin |
| Warm policy 2, changed exposure | same key; 200.91 ms | 1.763 ms Open + 2.059 ms prepare | 9.86 s / 436.1 MB | 1.024 s after Begin |

Whole-process time includes engine/shader/world setup, fixed waits and teardown;
it is **not** inverse reconstruction time. Disabled was the first/cold process.
The 53 KB warm file is deliberately an empty diagnostic, so it establishes actual
map-load cache plumbing only. Photometric fields are tested in the separate native
workload. No accepted original-stock lighting model is fabricated for this test.
The original 57-map audit and its physical uncertainty remain unchanged.

The current production dependency pass deliberately rereads and hashes source
content on each non-disabled map load; it avoids expensive CPU observation
derivation, fitting and tracing, not all disk reads. A verified archive-content
index, reduced metadata copies, explicit layer eviction and large-file streaming
are performance opportunities. Selective loading is by dataset, not arbitrary
per-probe paging. Requested data must be resident before sampling.

| Required failure/control | Executed coverage |
| --- | --- |
| BSP/material/sidecar/calibration mutation; order invariance | Native dependency content-key tests, including final external-deluxe page and absent→present transitions |
| Field representation; emitter movement; exposure | Derived-key tests, stale payload source binding, real hoth3 exposure key equality |
| Corrupt metadata/directory/checksum, truncation, unknown schema, limits | Existing disk `CorruptionAndLimits/NativeCache` plus selected corrupt/unknown field encoding and stale geometry tests |
| Disabled/cache-only miss/force rebuild | Existing lifecycle suite and early-preflight tests; real disabled/miss/hit/policy-2 renderer runs |
| Interrupted write, denial, reader retention, concurrent writers | Native observer/exception tests, abrupt child exit and controlled Windows legacy comparison |
| Deterministic chunked build | Independent native A/B byte hashes; 512 supported field samples across 19 total chunks |
| World change/stale lease; no query I/O | Existing lifecycle/API suites plus pending-product publication/abort and 1,000 resident queries with zero allocations |
| Unknown scale/conditional source semantics | P01/P06/Producer regressions retain physical API rejection and conditional metadata; loading does not alter them |

The MP renderer builds, and a changed SP/MP material-context identity changes
the key. An independently launched MP material-interpreter runtime benchmark
remains unexecuted; the SP hoth3 witness is not labelled MP.

P05 was committed first as `f56fc3e80`. V01 measurements use that revision plus
this documented working-tree implementation; GCC 16.2 CPU Release and MSVC 2019
SP/MP Release on Windows. Final numerical/performance witnesses are under
`tools/rend2/cache_validation/` (no licensed map geometry).

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2
build/s06-gcc/lightrecon_persistent_test.exe C:/absolute/writable/test-root
python tools/rend2/test_lightrecon_atomic_windows.py --output build/new-atomic-witness
python tools/rend2/validate_lightrecon_cache.py --native-test build/s06-gcc/lightrecon_persistent_test.exe --producer-test build/s06-gcc/lightrecon_producer_test.exe --output build/new-native-cache-witness
# After the native fixture was built; geometry/source inputs are the owned test fixture:
build/s06-gcc/lightrecon_persistent_test.exe C:/absolute/test-root/a/prepared benchmark
python tools/rend2/test_lightrecon_cache_runtime.py --installation "C:/Program Files (x86)/Steam/steamapps/common/Jedi Academy/GameData" --binaries build/v01-binaries --renderer build/s13-renderers/Release/rdsp-rend2_x86_64.dll --native-test build/s06-gcc/lightrecon_persistent_test.exe --output build/new-runtime-cache-witness
$env:CL='/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

`test_lightrecon_cache_runtime.py --help` documents actual map-load testing. It
uses a private home/stock-only VFS, 320×240, picmip 3, no HDR/shadows/lighting
consumers. Its seeded asset is explicitly an **empty cache-I/O diagnostic**,
not a fitted stock model, not positive source evidence and not an absence certificate.
Known-source native tests independently validate actual field/shadow/contribution
payload preparation. Thus a successful renderer metadata hit does not overstate
stock reconstruction or automatic production derived-layer selection.

Remaining production work: install an explicitly configured inverse-solver input
adapter/import receipt with verified material interpretation; provide owner-selected
derived-layer requests for future consumers; add layer eviction and measured large
asset streaming/compression optimizations. Linux/macOS atomic code remains supported
in source but requires platform execution in CI; Windows results do not certify it.
