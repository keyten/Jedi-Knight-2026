# X19 — contribution queries and opt-in MP controls

2026-11-08. Source baseline: `07241cc39` (X18). Exact working source/native hashes,
paired V02 receipts and validation results accompany this report in
[`tools/rend2/runtime_hardening/results/v1`](../tools/rend2/runtime_hardening/results/v1).

## Status

The production successful `SampleContribution` path is allocation-free on the
unchanged V02 workload. Source/component metadata and publication lifetime are
retained. Existing unavailable, ambiguous, corrupt and legacy opt-in checks remain
active. The new native workload is an allocation regression in CTest.

The MP harness has stricter scene, pause, camera, visible-world and independent
A/A gates. **No MP process was authorized or launched for X19. Actual frozen-world
A/A validation is Unperformed.** Synthetic log/image/control tests validate the
classifier and safety guards; they are not evidence that an actual MP scene was
successfully frozen. This remains an explicit incomplete runtime DoD item.

No normal renderer consumer, lighting equation, lightmap, dynamic light, sun,
shadow map, fog, source confidence or cache payload semantics changed.

## Query change

Two previous per-call heap allocations came from copying the long conditional
observation-domain string into `ContributionSample`, then copying the result
through the successful ternary return. The result now contains
`ConstRecord<ContributionReference> metadata`, an aliasing shared immutable lease.
`Domain()` returns the record's domain by reference. The ordinary successful
return returns the local result directly.

`Attribution::Prepared::Contribution` aliases its already prepared record/data.
`Snapshot::SampleContribution` validates the provider output first, including the
conditional domain, then aliases the canonical reference in the exact publication.
Attaching canonical metadata **before** validation would conceal a corrupt domain;
the negative provider test prevents that. This is an ownership change, not a new
decomposition model or a pre-encoding/physical-unit conversion.

No new shared-pointer control block, string/vector copy, lazy cache, file access,
geometry trace or source fit is created by a successful query. Preparation still
owns/copies metadata once. `Audit`/`MayReplace` remain separate operations; this
report does not claim those offline diagnostic operations allocate nothing.

The complete reference preserves sources, sample domain, component, quantity,
unit, calibration, configuration, source-root/path membership, uncertainties and
replacement policy. Missing/failed query results have no metadata lease. Metadata
presence does not establish source correctness or replacement permission.

### C++ API and lifetime

This is an intentional C++ API migration: replace the old owned `.domain` string
with `.Domain()` or `.metadata->domain`; recompile consumers. Canonical asset
schema and contribution disk codec are unchanged. Serialized strings remain in
the canonical reference and are not replaced with pointers.

```cpp
auto response = snapshot.SampleContribution(query);
if (response) {
    auto retained = response.value.metadata; // independently retained const lease
    const std::string& domain = retained->domain;
    // Read source IDs, quantity, calibration and membership through retained.
}
```

`Domain()` is borrowed: keep the result or its metadata lease alive while using
the returned reference. Do not retain a string reference returned from a
discarded temporary result. Copying the result or metadata lease allocates no
new metadata. A retained result pins the old immutable publication, matching the
existing `ConstRecord` policy. It remains valid after owner reset/map change;
drop result/record leases to release old CPU buffers. New-map queries still reject
old generation handles. Backend-direct results similarly retain backend metadata.

Tests retain a result after reset and after every caller snapshot is destroyed,
read its original source/domain/calibration/path order, then verify the weak
snapshot expires after the last result lease is released. A corrupt legacy
provider domain remains `Corrupt`; unavailable/ambiguous/corrupt provider statuses
are preserved and their partial payloads are discarded. NaN lighting is rejected.

## Same V02 workload

No new lighting fixture substitutes for V02. The existing
`lightrecon_validation_tool` still creates the same prepared P01/P05/P06 backends
and native S13 supplied-source contribution artifact. Each measurement uses
4096 batches x 8 calls, after 128 warm-up calls. The source inputs are known
controlled inputs, not blind discovery or stock source truth.

Three before/after pairs use a saved pre-X19 executable and the new executable.
Native identity and parameters are frozen before measurement. Every pair retains
32768/32768 successful contribution queries; all other measured query success
and Unavailable counts match. In particular, indirect-only queries on a sky field
and missing region lookup remain Unavailable, not successful black lighting.

| Repeat | Before allocations | After allocations | Before p50/p95/p99 ns | After p50/p95/p99 ns |
|---|---:|---:|---:|---:|
| 1 | 65536 | 0 | 375/625/625 | 125/125/250 |
| 2 | 65536 | 0 | 375/625/625 | 125/125/250 |
| 3 | 65536 | 0 | 375/625/625 | 125/250/250 |

The predeclared V02 allocation maximum is zero and p95 soft budget is 250000 ns.
The new path passes both. This measures C++ `new/new[]` and the actual ordinary
query implementation; it is not a sampling profiler estimate. The implementation
has no direct `malloc`/allocator call on that path either. Timer granularity is
coarse (one batch quantum is 125 ns/call here), the host is shared and timings
are not an isolated universal 5x speedup claim. This compact source catalog does
not benchmark every stock-map reference count or GPU/frame performance.

`lightrecon_query_workload` is now part of CTest and fails on any contribution
allocation, retaining its numerical output. Existing API/P06 tests additionally
cover metadata, source generation, sparse zero versus unknown, corrupt chunks,
unsafe replacement and status preservation. The P06 reference suite repeats
identically: 504 assertions, zero failures.

## MP control repair

The frozen V02 MP v4 logs had `cl_paused=0`, `sv_paused=0` and camera motion. They
remain failed historical controls. They have not been rescored as successful
runtime regression.

The original freeze-menu list was appended to stock `jampingame.txt` after its
outer closing `}`. `UI_LoadMenus` stops at that token, so the appended load was
outside parser scope. The custom list is now **prepended**, before stock content,
for both ingame and main lists. Stock archives are only read; an isolated loose
UI file is generated for a future authorized test. The MP UI script owns pause;
the harness does not pretend a console write to read-only `cl_paused` is enough.

The declared mp/duel4 actor camera is (-2880, 2036, 50), yaw 90, FOV 80. Actual
rendered eye position is read back via `viewpos`, including the game's eye-height
offset; it is not silently assumed to equal the actor origin. Inputs are released,
mouse input is disabled, then the UI freezes the scene. No unprovided camera for
mp/ffa3 is guessed; that case is Unperformed until a protocol is provided.

Five capture windows are required: AA0, AA1, A, B, repeat. AA0/AA1 use identical
settings before any A/B change. Each requires:

- Exactly one complete diagnostic block and observed disabled consumers.
- Both client/server pause equal to 1.
- Actual requested-map camera and FOV; identical across every capture.
- Live `r_speeds 1` world counters with positive surfaces, BSP leaves and vertices
  during the screenshot window. A historical `glslusage` listing is insufficient.
- A nonflat image with at least 64 distinct RGB values.
- Passing AA0/AA1 comparison before accepting A/B or repeat comparisons. Existing
  image tolerance is unchanged: max channel delta 1, <=5 differing pixels,
  mean <=1e-5. Motion is an invalid control, not a lighting regression.

The harness uses the existing bounded renderer counters; no production renderer,
shader, UI engine or gameplay code was added for these receipts. Minimal settings
remain 640x360, picmip 3, 30 FPS, low subdivision/LOD and 256 shadow-map setting
where the selected renderer profile needs it. This is not a reconstruction light
consumer; build/use controls cannot create recovered illumination.

## Authorization and unperformed cases

The user's DoD requires explicit MP permission. No such permission was received
for this run. The four requested map/profile cases are recorded as `Unperformed`,
with zero MP processes and no runtime image score. A successful synthetic image
classification cannot change this status to Passed.

Both `run` and direct `run_case` calls enforce permission before asset preparation
or process work. The CLI retains the explicit `--allow-mp-launch` gate. Default
owned launch budget is one, with no automatic retries; later unexecuted cases
remain Unperformed after a failed/unsupported control or exhausted budget.
Independently running games are preserved. Missing runtime inputs are Unperformed.

The harness does not change system network/firewall rules, confirm Windows
prompts or set `net_ip`, `net_port` or `sv_lanForceRate`. A future authorized
local MP engine may still open its normal listen-server sockets and prompt
Windows; the harness neither suppresses nor approves that prompt. Test-only
game/renderer settings live in an isolated home. One executable path is reused
within a batch, and copied engine/renderer binaries must match their input hashes;
stale files are rejected rather than silently reused.

No actual visible-world A/A result is supplied here. Closing that DoD item needs
the separately authorized single MP run and its retained screenshots, camera,
world counters, pause receipts and binary/configuration identities.

## Builds, regressions and timing caveat

46/46 CPU tests pass, including the added real query-workload test. Both SP/MP
Rend2 Release DLL builds pass. 29 Python tests cover report/permission controls,
menu-list ordering, frozen and moving cameras, no-world menus, missing telemetry,
flat frames, failed A/A, missing runtime inputs, one-launch budget and no implicit
process/preparation without authorization.

All 27 disabled shader programs / 51 stages compile/link on the driver; shader
source/binding difference from X18 is zero. The historical `380ee0f7` baseline
still has the existing 7278-line drift and is not rewritten. Game visual checks
were not performed.

The complete CPU suite reported 2142.81 s, dominated by point-refine (2142.76 s),
versus 221.40 s in the earlier X18 run. No point-refine equation, work budget or
test source changed in X19, and that test does not call `SampleContribution`.
This uncontrolled wall-time outlier is retained, not averaged away or declared
an optimization. A separate bounded repeat completed in 263.85 s inside the
native test with **1500 assertions and zero failures**; the 2142-s duration did
not reproduce. Its OS/per-case resources are in the machine receipt. This is
still a shared-host comparison, not proof of an isolated optimizer speedup.
Query timing is
measured independently on the same V02 workload; it does not explain or certify
scene-wide optimizer performance.

One development assertion initially expected calibration ID 1 in the lifetime
test, while the existing authored fixture uses ID 2. The test now compares the
retained value to the original immutable input record; no calibration or numerical
payload was changed. The initial failed assertion receipt is preserved.

## Reproduction

Use fresh output directories. With the same w64devkit compiler on PATH:

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc --parallel 2
& $py -X utf8 tools/rend2/runtime_hardening/run.py --before build/x19-baseline/lightrecon_validation_tool.exe --out build/x19-queries-final
& $py -X utf8 tools/rend2/contributions/benchmark.py --output build/x19-contribution-controls-v2 --tool build/s06-gcc/lightrecon_contributions_test.exe
& $py -X utf8 -m unittest tools.rend2.validation.test_mp_control tools.rend2.validation.test_report tools.rend2.path_support.test_reports tools.rend2.producer.test_controller tools.rend2.appearance.test_report
ctest --test-dir build/s06-gcc --output-on-failure -j 2
$env:MSBUILDDISABLENODEREUSE='1'; $env:CL='/MP2'
cmake --build build/x16-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 1 -- /nr:false
& $py -X utf8 tools/rend2/lightrecon_regression.py --cpp C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/gcc.exe --output build/x19-disabled-gpu
& $py -X utf8 tools/rend2/producer/compare_revision.py --revision 07241cc39 --gpu-report build/x19-disabled-gpu/report.json --out build/x19-disabled-delta
& $py -X utf8 tools/rend2/runtime_hardening/report.py --out tools/rend2/runtime_hardening/results/v1
```

The pre-X19 executable must be built from X18 `07241cc39`; its saved hash is in
the protocol. No proprietary stock assets, binary executables or unperformed
screenshots are committed. The optional future MP CLI is documented by
`validation/runtime_mp.py --help`; use explicit map/profile, fresh output,
matching input binaries and `--max-launches 1`, only after user authorization.

The remaining runtime limitation is concrete: no actual MP frozen visible-world
A/A has been validated in X19. No source restoration or new renderer capability
is claimed by making queries allocation-free.
