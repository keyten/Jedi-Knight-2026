# V03 reconstructed-lighting diagnostics

2026-11-04. Inspection works before and after canonical publication. The preceding
V01 change is committed as 0c0709ca4. These diagnostics enable no lighting consumer.
Original-stock physical lamp recovery remains unaccepted.

## Architecture and interpretation

Core::Diagnostics reads canonical records, actual Joint placement receipts,
production forward results and existing prepared P01/P02/P05/P06 data.
Joint::Request::diagnostics is an optional synchronous, bounded observer. Its
return cannot affect fitting; it is absent from numerical/cache identities.
The owned comparison preserves model keys, state, 28 forward calls and 168 rays.
No light equation or field evaluator is reimplemented.

Canonical schema 8 and the V01 .jklr format are unchanged. The new .jkdbg file is
diagnostic interchange. It cannot install lights, fields or CPU capabilities.
Renderer imports verify the original BSP content hash and bind to the current
world ticket. World begin, aborted load and unload release diagnostic leases.

JSON uses decimal strings for stable 64-bit source/sample/region/configuration
IDs, preserving values beyond JavaScript's exact integer range. Coordinates are
JA world space, +Z up, with explicit world-units-per-metre. Emitter forward is
photon travel; receiver-to-sun is its negative. Tube axis is frame.right.
Sun widgets use a labelled drawing anchor, never a finite sun position.

Reports distinguish availability, hypothesis state, evidence and suitability.
Unknown estimates are null. Dataset presence and installed sampling capability
are different. Per-dataset resident bytes remain null for metadata-only assets;
actual V01 cache reservation/read/preparation metrics are retained. Neither
Available nor Resolved establishes exact-light/replacement suitability.

## Commands

SP and MP register r_lightReconDebug. All layers default off. There are no new
archived cvars or shader permutations.

| Subcommand | Work and result |
|---|---|
| info | Counts, mask, last active overlay submissions/clips/microseconds, help |
| load name | Read lightrecon-debug/name.jkdbg; validate hash/schema/numerics/checksum/bounds; layers initially off |
| asset | Bounded canonical source markers, including separate alternatives |
| layer sources 1 | Enable one layer; 0 disables; all selects every layer |
| select source 123 | Exact stable ID filter; 0 clears |
| select region 123 | Region filter, without treating regions as opaque rooms |
| select configuration 123 | Configuration-specific filter |
| source 123 | At most 64 matching records, reasons, domains, profiles; absent is Unknown |
| report name | Human summary and lightrecon-debug/reports/name.json; no fitting |
| sample indirect field px py pz nx ny nz | Immutable physical irradiance query, strict Lux contract |
| sample radiance field px py pz dx dy dz | Physical incoming-radiance query, Nits, direction toward incoming light |
| sample conditional field px py pz nx ny nz [configuration] | Explicit conditional normal-irradiance query; no implicit profile/SI promotion |
| prepare | Explicit bounded static-geometry BVH preparation |
| geometry | At most nine camera rays and four geometry markers, actual camera/hit/normal IDs |
| ray source sample | Bounded static centre segment using imported IDs; not area integration/source feasibility |
| off | Clear selection and release imported scene/BVH buffers |

Layers: sources, placement, dynamic, receivers, direct, sun, sky, indirect,
unresolved, residual, probes, field, visibility, transport.

Accepted geometry is green; conditional orange; rejected red; deferred blue;
budget yellow; unavailable grey; unknown magenta; visible cyan; blocked orange-red;
invalid white. Geometry acceptance is not source-existence confidence.

Existing original-data tools remain: r_lightReconInfo, r_lightReconObservations,
r_lightReconLuxels, r_lightReconEvidence, r_lightReconExport. Their decoding
contracts remain intact. The S13 r_lightReconPreview retains its explicit
unfitted-prior label. Old runtime lightgrid debug cvars are not repurposed.

Prepare/geometry require explicitly captured source geometry: reload with
r_lightReconBuild 2/3, consumers off. Cache-only/disabled loading is unchanged.
The renderer still has no automatic inverse-fitting adapter. Geometry inspection
uses the offline capture's explicitly logged 8-WU patch approximation bound;
curved/unknown optical intersections retain uncertainty. Solver trace tolerances
are unchanged. Displayed geometric normals follow original triangle winding;
back faces are not silently flipped to shading normals.

The overlay scans at most 16,384 records, submits at most 2,048 records per frame,
and clips beyond 4,096 WU/behind the camera. Submitted is not GPU-visible.
Walls, fog or a rejected centre inside solid can hide markers. Inspect screenshots
and camera coordinates. No selected scene means an early return before shader
registration, geometry lookup or poly submission. Last active measurement
counters can remain visible after off; no geometry remains active.

## Offline APIs, domains and storage

The bounded builder is in shared/rd-rend2/lightrecon/diagnostics.h.

| Input | Diagnostic output |
|---|---|
| Canonical emitter | Origin, axes/cones, rectangle sides, finite open tube, position uncertainty |
| Actual Joint::Place receipt | Predicate, from/to, clearance box/radius, feasibility, region, support contract/fixture, occupancy assumptions |
| Dynamic boundary | Original model ID/bounds, swept-extent status, unknown bake participation |
| P02 topology/query | Valid/relocated/invalid probes, relocation vectors, interpolation neighbours/weights, absent support |
| P01 field artifact | Reference/representation errors and six axis evaluations for at most 16 probes; radiance and irradiance distinct |
| Ray/P05 shadow artifact | Static visible/blocked/unknown/conditional, hit surface/material/distance, finite-source bounds |
| P06/Producer artifact | Existing roots/path orders/configurations, quantity/unit/domain, composition and replacement proof metadata |

Diagnostic source predictions call production Shape::Prepare/Block::Evaluate
and Bake::Encode. They use the candidate's supplied initial amplitude prior,
not a claimed fitted amplitude. Compiler-relative incident and isolated encoded
predictions are separate. The encoded experiment compares one isolated candidate
against the observed total. It is not a joint total and is not additive after
shared-peak/gamma encoding. No formula is ported to Python.

For an encoded prediction p and admissible interval [l,u), discrepancy is zero
inside, p-l below, p-u above. A saturated channel has no upper bound.
Numbers stay finite; censorship is explicit. Signed discrepancy is not GI.
Unavailable pairs have no numeric value and remain in the exported denominator.
Only training targets enter additional diagnostic forward work.

Each map-solver profile writes isq-debug / linear-debug .jkdbg, JSON, CSV, OBJ
and human/JSON reports. The interval-residual PPM is a 32-column record strip,
not a lightmap atlas: maximum absolute channel, fixed scale 0..32 encoded values,
signed colour, unknown magenta. Existing receiver-support manifests, selected
geometry, scheduler placement/unit tables, fixture rays and transport
mapping/overlap/omitted-link tables are retained.

Exports work without an accepted asset. The observer records actual placement
checks. Pre-query rejections (bounds, invalid parameters, missing moving-emitter
pose) remain in Joint attempt/rejection tables; no fictitious trace is added.
The existing S09 Shadow::Objective Inspect/DebugTsv/DebugObj paths and
lightrecon_shadow_inspect retain scoped edge/caster/source disagreement export.
Region graph DebugObj and region inspect/audit tools retain connectivity geometry.
V03 reuses them; a centre-ray drawing does not establish an edge association.
P06 roots, path orders, configurations and replacement proofs are reused.
No sparse absence, missing field or unknown visibility becomes successful black.

FrozenObservations reads existing S13 sample/observation codecs to export the
final model total, original intervals and model-minus-observed discrepancy.
It never adds encoded component arrays to manufacture a total. S13's stored
target-minus-model discrepancy has the opposite sign, explicitly labelled.
The controlled producer roundtrip checks eight totals/eight interval residuals,
eight source-root records and corrupted payload rejection. Unknown domains leave
comparison unavailable. Reports retain configuration amplitudes/domains, source
count ranges, factor-specific uncertainty, calibration population/event and
descriptive position scatter.

The .jkdbg header is JKLIGHTDEBUG1. UTF-8 strings are hex-encoded; numeric output
uses classic locale/full precision; a trailing SHA-256 seals the payload.
Records have a canonical full-field order. Import requires a nonzero expected
map hash. Limits: 16 MiB, 16,384 records, 4,096 bytes per string. Corruption,
truncation, unsupported schema, wrong map, nonfinite values and limits fail closed.
Omitted counts explicitly mark partial diagnostics.

lightrecon_diagnostics_tool validates interchange and exports JSON/CSV/OBJ/PPM
without a renderer. lightrecon_probes_tool also writes geometry-only diagnostics
for three bounded ROIs. Probe construction creates no coefficients or light energy.

## Numerical and geometry validation

Aggregates: [validation.json](../tools/rend2/diagnostics/results/v1/validation.json).
Licensed geometry, observations and screenshots remain private. Original
v4/X12/X15 witnesses are preserved.

The stock runs use the actual native scene scheduler and Joint, conditional bake
occupancy, receiver preflight and independent inverse-square/linear NetRadiant
hypotheses. Compiler identity and absolute physical scale remain unknown.

| Map | Train/validation/heldout geometry | Records isq/linear | Numeric isolated encoded pairs / 32 | Unknown centre rays isq/linear | Time / peak RSS |
|---|---:|---:|---:|---:|---:|
| mp/duel4 | 113/93/72 | 3,538 / 3,537 | 13 / 12 | 8 / 5 | 5.828 s / 39.77 MiB |
| hoth3 | 412/239/227 | 4,350 / 5,404 | 0 / 13 | 8 / 10 | 28.765 s / 109.83 MiB |

hoth3 detail is capped at 128 training receivers; duel4 preserves all 113,
including censored channels. Final diagnostic omissions are zero. Unsupported
receivers/pairs are explicit. The 32-pair scope is two candidates × sixteen rows.
Direct record counts include incident and encoded values, not unique sources.
Both maps still end DiagnosticOnly: no accepted canonical source model, final
heldout callback or source uncertainty fit. Profile files are alternatives.

duel4 repeat uses identical native executable, inputs/settings. Both .jkdbg files
match byte-for-byte. Timing/RSS stay outside numerical witnesses.

The owned diagnostic regression has 67 checks: all source shapes/unknown positions,
IDs beyond 2^53, shuffle invariance, corruption/truncation/map/enum/nonfinite/limit
rejection, missing confidence, censored intervals, invalid finite geometry,
quantity-specific SH/SG, absent probe support, selection and retained snapshot
lifetime. The observer leaves numerical output/work unchanged.

Full relink CPU testing passed 45/45 (331.81 s; earlier run 201.62 s). The final
diagnostics-only ledger/parser extensions pass the 67-check native diagnostic test
and 6/6 focused contribution/field/probe/shadow/lifecycle/diagnostic regressions.
Final SP/MP Release builds pass. Existing x64 /arch:SSE2 warnings are unrelated.
Importing and exporting the 1.3-MiB duel4 interchange with the standalone tool
takes .993 s and 11.67 MiB process peak on this concurrent Windows host.

hoth3 geometry-only probe export takes 6.125 s, 100.89 MiB process peak. Three
ROIs have 777/523/530 valid probes, 4/69/5 relocated probes and about
1.00/.91/.91 MiB topology. No stock irradiance accuracy is claimed.
Existing field/angular/shadow/contribution tests independently cover controlled
physical references.

## Actual game inspection

SP Rend2 ran sequentially on hoth3, hoth2, vjun2 and mp/duel4 with private homes,
stock archives, 640×360, picmip 3, 30 FPS, HDR/dynamic lighting/shadows/physical
consumers off. The harness refuses other running OpenJK processes and limits
its own child to 180 seconds/1 GiB. It records camera/hit IDs, normals, settings,
hashes and existing before/geometry/ray/after images.

| Map | Geometry markers | Last overlay CPU submissions | Before/after changed pixels |
|---|---:|---:|---:|
| hoth3 | 3 | 15.2 us | 0 |
| hoth2 | 3 | 19.1 us | 18 |
| vjun2 | 4 | 21.3 us | 938 |
| mp/duel4 | 4 | 2.8 us | 0 |

Actual process peaks and image hashes are in validation.json. These are last-frame
CPU submission times, not total GPU/frame overhead. hoth3 shows wire geometry at
ceiling/wall intersections; vjun2 shows surface markers in its multi-level interior.
hoth2 fog/snow strongly limits contrast: its small marker footprint was inspected,
without claiming sun/shadow correctness. duel4 loads rejected/conditional candidates
and placement boxes without an accepted asset. Many hypothetical centres remain
occluded at spawn; exact fixture alignment is not certified by submission counts.
Their XYZ/predicates are available in the spatial exports.

vjun2 retains temporal scene drift under pause/fx-freeze, so its before/after is
not a bitwise unchanged-rendering proof. hoth2 has smaller drift. hoth3/duel4
recorded image content matches before/after exactly. Consumer checks and code
inspection independently show no changes to illumination, fog or shadow passes.

Early attempts remain preserved: 0.01-WU patch accuracy exceeded subdivision
limits; broad local extraction exceeded its triangle cap. Neither counted as
visual success. Final geometry inspection uses bounded camera rays/local hits.
Some screenshot requests were logged but not materialized before disconnect;
only actual files are listed as captures.

Final duel4 command smoke: physical indirect/radiance return Missing; conditional
query without a selected field/configuration returns InvalidInput; missing IDs
and files remain unavailable; unload has zero records/mask. These are not black
lighting results. There is no accepted stock sun/calibrated GI/source field to
certify visually. Controlled tests establish implementation conventions only.

## Reproduction and remaining scope

PowerShell at repository root, with the installed CMake/GCC toolchain on PATH:

    cmake -S tools/lightrecon_test -B build/s06-gcc
    cmake --build build/s06-gcc -j 2
    ctest --test-dir build/s06-gcc --output-on-failure -j 2
    build/s06-gcc/lightrecon_diagnostics_test.exe build/v03-owned
    build/s06-gcc/lightrecon_diagnostics_tool.exe build/v03-owned.jkdbg 0400000000000000000000000000000000000000000000000000000000000000 build/v03-export
    cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2

With private observations and matching BSP hash:

    build/s06-gcc/lightrecon_map_solver.exe <all-pages.jkobs> <fresh-output-folder> <bsp-sha256> scene-receiver-preflight
    build/s06-gcc/lightrecon_probes_tool.exe <all-pages.jkobs> <private-probe-report.json>
    python tools/rend2/diagnostics/run_runtime.py --installation "<JA GameData>" --binaries build/v01-binaries --renderer build/s13-renderers/Release/rdsp-rend2_x86_64.dll --output "<fresh-private-folder>" --candidates "<duel4/isq-debug.jkdbg>"

Copy the selected map/profile interchange into the private OpenJK/lightrecon-debug
directory before importing. Private run locations: %TEMP%/jk-lightrecon-v03-runtime-v3,
jk-lightrecon-v03-runtime-final2, jk-lightrecon-all-stock-20261031/runs-v03-v4 and
jk-lightrecon-v03-probes-v1.

No full-map final predicted image exists without an accepted model; the heatmap
is sample-indexed. Labels/details are console/JSON, not world text. Angular views
use six axes, not a dense atlas. Fields/probes/shadow artifacts require prepared
providers; cache misses do not generate them. Unknown occupancy, alpha optics,
historical transfer and source allocation remain unknown. Camera focus/x-ray
annotation and dense angular plots are future UI improvements, not grounds for
relaxing numerical source admission.
