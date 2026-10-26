# Reconstruction infrastructure series audit

Audit base: `a4f52c5cb`, branch `rend2-unified-wip`, 2026-10-26
(Australia/Sydney). Scope: the 18 commits from `0851b8563` through `a4f52c5cb`,
their production boundaries, public contracts, tests and recorded map evidence.
The versioned [validation witness](../tools/rend2/lightrecon_baseline/series-audit-validation.json)
records the reviewed revisions, hashes and fresh results.

## Verdict against the three requirements

The production infrastructure meets the three architectural requirements below.
That is not a claim of reconstructed lighting, complete stock-map geometric
classification, physical calibration of historical bakes, or solver accuracy.
One colour-comparability bug was reproduced and fixed during this audit.

### No false reconstruction success

`Core::Session::Begin` returns `Unavailable` without an injected algorithm,
before reading observations. Supplying nonempty emitter evidence does not
change this. The renderer `RendererWorldSource::Prepare` returns `Unavailable`;
it installs neither a fitter nor a synthetic algorithm. The current runtime
therefore reaches `BuildRequired/Unavailable`, not a reconstructed map.

Original observations, material/source candidates and region graphs are
separate immutable input types. Extraction does not write them into
`ReconstructedLightingAsset::emitters`, mark them resolved or certify physical
confidence. Sun shader declarations are evidence; the renderer's default sun
vector is not substituted. Transport/aperture metadata is not a physical lamp.
The deleted grid algorithm has no production files, types, controls or GPU
injection left; historical snapshots and negative tests are explicitly separate.

`Status::Ok`, API `Ready`, lifecycle `Available`, and session `Completed` are
storage/publication/protocol states. They do **not** certify recovered lighting.
A valid empty asset can be loaded and inspected; all lighting capabilities remain
absent. Candidates alone cannot enable exact-light consumers. A future producer
must report accuracy, coverage, unresolved hypotheses, calibration and diagnostics
separately. Structural validation cannot prove the honesty/accuracy of a producer.
Comments on `Completeness` and `SessionState` now make that boundary explicit.

### Units and provenance survive every boundary

| Representation | Meaning and guard |
| --- | --- |
| Raw RGB8/RGBA8 | Original encoded bytes, channel saturation/quantization; not lux/radiance. |
| Raw external HDR/grid float | Original decoded floats with source encoding; linear representation does not establish physical scale. |
| `Decode::source` | Source-linear relative candidate under a documented transfer or explicit assumption. Unknown transfer remains unavailable. |
| `Decode::renderer` | Separate reproduction of renderer transformations, including lossy presentation operations; not silently used as source evidence. |
| `Decode::scene` | Optional reference-calibrated scene equivalent with gain, reference, method and provenance; absolute scale remains unknown. |
| Physical emitter output | Candela for point/spot, nits for rectangle/tube, lux for directional. Calibration distinguishes physically authored, legacy scene equivalent and uncalibrated relative. |
| Candidate intensity | `Unknown`, relative shader, legacy bake-tool or explicitly authored physical units; glow and `q3map_surfacelight` do not become nits automatically. |

`r_pbrWorldUnitsPerMeter` is geometric scale; `physicalToScene` is canonical
presentation scale, not measured historical bake brightness. The disk format
and offline archive preserve unit/calibration metadata rather than converting
everything into an unlabeled RGB buffer. Output validators reject contradictory
relative/physical calibration and invalid units; physically authored provenance
cannot be inferred directly from legacy/reconstructed observations.

Fully saturated LDR channels remain censored lower bounds. No inverse of clipping,
chromaticity normalization or unknown bake albedo is invented. Material texture
colour is not divided out of baked observations. Lightmap and lightgrid scales
are not assumed equal merely because they share a map or geometry unit.

**Fixed defect:** `Decode::Compare` previously accepted `Unknown == Unknown`
primaries as a common relative colour basis when `relativeDomain` matched. A new
fixture failed on the old code. The comparison now requires a known matching
basis; otherwise it returns `Incomparable`. This changes only the explicit CPU
comparability query, not raw/decoded values, serialized schemas or rendering.

### Consumers remain independent

The sole gate is `EvaluateConsumer`: master AND that consumer's request AND the
required asset capability AND the feature adapter's actual availability. It
does not read another consumer's requested flag, start a build, mutate the asset,
set cvars or allocate GPU resources. Build/load policy is independent and applied
at the main-world boundary. The shared SP/MP registration defaults all twelve
controls to zero, with accurate next-map-load versus live-use descriptions.

All production canonical renderer adapters currently report unavailable, even
if unrelated fog/PBR/Forward+/LTC switches are on. Future integration must install
and test each adapter deliberately; an existing technology toggle is not proof
that canonical consumption exists. New tests enable each of the ten consumers
with all nine others off, as well as the existing all-on/one-off cases. The same
snapshot identity and intensities remain unchanged.

Normal NPC/Ghoul2 lighting, optional original-grid entity probes, ordinary fog
B/S preparation, sun visibility/shadows, PBR calibration and authored/automatic
LTC retain independent owners. For example, entity probes are built directly
from the original BSP grid without requiring fog or reconstruction.

## Commit-by-commit coverage

| Commit | Boundary checked | Conclusion / limits |
| --- | --- | --- |
| `0851b8563` | Canonical asset, IDs, validation, source/transport distinction | Renderer-independent metadata, explicit uncertainty/calibration; no solver. |
| `31c1b9e54` | Read-only snapshots, capability/query semantics, generations | Pinned immutable leases; unavailable sampling is explicit; no fallback SH/falloff. |
| `719d1c15b` | Shared input, chunk providers, builder/session, CMake | One C++11 CPU archive for SP/MP/tools; no engine/GPU globals; default algorithm unavailable. |
| `60d1cc8c6` | Shared registration, policies 0/1/2/3 and consumer gates | No cache-only build or technology force-enable; empty assets do not enable lighting. |
| `1ed49a88d` | Controlled disabled shader contracts and runtime protocol | Current 27-program matrix compiles and matches reviewed baseline. |
| `9c30779b1` | Versioned asset codec, content key, limits, checksums, atomic I/O | CPU round trips/adversarial tests pass; complete runtime dependency collector is absent. |
| `c7f80d31b` | Main-world lifecycle, leases, abort/unload/restart, MP sub-BSP | One scoped owner; synthetic cache/build paths tested; real cache hits not claimed. |
| `f55fd01cc` | Raw image capture before transforms/atlas/free | Logical pages and original pixels retained only for requested input capture. |
| `3a88e68ca` | Source/renderer/scene decode and photometric assumptions | Explicit stages and clipping; comparability defect fixed by this audit. |
| `fa7b8a7b5` | Raw indexed lightgrid, direction/coordinates, styles/HDR | All cells remain evidence; style/brightness is not free-space certification. |
| `e496cd0e9` | Source UV luxel geometry, area, normals and ownership | Bounded mapping, padding/overlap flags; unsupported/ambiguous cases remain excluded. |
| `0b44d2041` | Deluxe correspondence, styles, vertices and materials | Distinct evidence types; no style merging or calibrated albedo/emission invention. |
| `e31e597ba` | Immutable BVH, optical/sky/bias/domain queries | Structured unknown/blocked/visible; PVS is not direct visibility. Patch/optical uncertainty is explicit. |
| `7c8122b96` | Adaptive region graph, typed adjacency/transport/LOS | Deterministic bounded structural graph; stock ROI segmentation remains conservative/incomplete. |
| `6eabb5439` | Geometry/masks/JSON/entities/sun/sky candidate provider | Provenance and aliases retained; no candidate promotion to runtime physical light. |
| `704b21871` | Offline observations, blind fixtures, schema/limits, partitions | Inputs separate from answers; raw full sources and partial derived/ROI coverage are distinguished. |
| `380ee0f7e` | Actual q3map2 bakes and SP export, independent BSP audit | Previously executed 12 maps; new offline revalidation described below. Legacy compiler units are not physical units. |
| `a4f52c5cb` | Removal inventory and independent legacy owners | Old fitter/proxies/portals/moment controls deleted; normal rendering has separate PBR/fog/entity/sun paths. |

This is an interface/critical-path code audit plus executable tests and replay,
not a formal proof of all geometry cases, hostile-input security or estimator
accuracy. No mathematical inverse estimator exists to evaluate.

## Fresh verification in this audit

| Check | Result |
| --- | --- |
| MSVC Release SP and MP renderer build | Passed, including relink against the changed CPU core. |
| Final MSVC CPU suite | 16/16; 1,777,346 checks. |
| Final GCC standalone CPU suite | 16/16; same 1,777,346 checks. |
| New negative comparability fixture before fix | Failed as expected: two unknown colour bases incorrectly reported comparable. |
| Existing shader/registration/dependency mutations | 15/15 passed. |
| q3map2 authoring recipe tests | 6/6 passed. |
| Disabled shader comparison with real driver compilation | 27 programs / 51 stages; zero differences, GPU interfaces and dependency audit passed. |
| Self-contained synthetic pipeline | 11 scenes generated twice; input/answer byte identity, import/remap/classification, spatial splits and wall/door visibility passed. No q3map2 execution in this fresh generation. |
| Existing actual q3map2 datasets | 12 maps, 25,731 independent planar world samples, five rejection cases and wall/door/sky/spot/bounce checks passed. Maximum fixture mapping error 3.18e-14 world units; this is not a general-map error guarantee. No fresh bake/game launch is claimed. |

**Observed test failure:** the first concurrent-build GCC run passed 15/16;
`lightrecon_offline` failed its native atomic replacement test while old readers
were retained. The exact OS cause was not captured in that run. The writer
returned an error instead of publishing success; the old readable dataset
remained unchanged. The failure did not recur in five sequential focused runs
or the final complete GCC run. The native adapter now includes the Win32 error
code and the test prints it on failure. This is improved diagnostics, **not** a
claim that an unidentified intermittent I/O cause was fixed. Original failure
and subsequent passing logs are retained in the witness.

Legacy renderer and GLSL paths are unchanged by this audit; the functional
changes are the explicit CPU comparison guard and failure diagnostics.
The earlier actual-SP cleanup evidence remains
historical evidence from its recorded DLLs: nine hoth3/hoth2 fixed static views
with zero pixel differences, consumer-toggle stimuli, map transitions/
`vid_restart`, raw-source hashes and a separate real saber/Ghoul2 fixture. Those
gameplay captures were not rerun or relabelled as captures of this audit's DLLs.
The old 15-test GPU run's initial 14/15 and corrected focused volumetric pass
remain recorded in [the removal report](rend2-grid-reconstruction-removal.md).
This audit freshly reran the 27-program disabled matrix, not the entire unrelated
water/cloud/atmosphere GPU suite.

## What is still unavailable or provisional

- **No inverse solver, production field evaluator or canonical rendering adapter.**
  Source existence, intensity, direct/indirect attribution and physical accuracy
  have not been recovered. Inputs/candidate counts are not a quality metric.
- **Runtime cache key is deliberately incomplete.** Codec, hashing categories,
  invalidation and atomic cache operations work in CPU tests, but production
  resolved BSP/sidecar/material/settings/calibration hashing still needs wiring.
  The renderer currently reports `NotCacheable` and does not accept a guessed key.
  A future production cache-hit path also needs early key/cache preflight before
  raw observation capture: policy 2 currently starts capture before the final
  owner lookup. The CPU capture module supports suppressing a validated hit,
  but the renderer cannot currently establish such a hit at that early boundary.
- **Historical scale is not measured.** q3map2 fixture source values have legacy
  tool units. Position/direction and raw/baked behaviour can be checked; absolute
  candela/lux/nits recovery cannot be certified from those LDR bakes. The separate
  CPU reference scenes have their own explicit physical forward-model answers.
- **Available data is not complete ground truth.** Old maps may lack light
  entities, deluxe/HDR pages, bake settings, style curves and bake-time material
  reflectance. Missing fields remain missing. Overlaps, malformed surfaces and
  unsupported patch mappings are excluded/uncertain rather than fabricated.
- **Stock region segmentation is not solved.** Recorded hoth3/hoth2 bounded
  export ROIs had 31,340/29,604 cells and **zero certified regions**. Full raw
  visual/collision geometry remains available, but this is not successful room
  recovery. Derived luxel exports use an explicit image prefix/ROI by default;
  they do not claim complete full-map derived coverage.
- **Optics and runtime motion remain limited.** Alpha/translucency, curved
  visibility approximations and moving brush boundaries report uncertainty.
  Legacy sun collision traces/style masks remain presentation approximations,
  separate from the canonical visibility/observation truth.
- **External compiler and evaluation limits remain.** Whole-BSP repeatability
  was not perfect in the historical q3map2 runs; exact same-BSP export identity
  was checked without normalizing away differences. Held-out RGB must still be
  withheld by the evaluator even though a complete dataset stores all partitions.
  MP gameplay, exhaustive GPU drivers/material permutations and large-map peak
  process RSS are not certified by this audit.

## Reproduction

PowerShell, repository root, existing local configurations:

```powershell
$env:PATH='C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin;'+$env:PATH
$py='C:\Users\Keyten\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
cmake --build build/lightrecon-core-renderers --config Release --parallel 4
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure
cmake --build build/lightrecon-disk-standalone --parallel 4
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
ctest --test-dir build/lightrecon-disk-standalone -R '^lightrecon_offline$' --repeat until-fail:5 --output-on-failure
& $py tools/rend2/test_lightrecon_contract.py
& $py tools/rend2/datasets/test_q3map2_recipe.py -v
& $py tools/rend2/lightrecon_regression.py --output build/reconstruction-series-audit/shaders
& $py tools/rend2/datasets/run_pipeline.py --tool build/lightrecon-disk-standalone/lightrecon_dataset_tool.exe --output build/reconstruction-series-audit/synthetic
git diff --check
```

Use a fresh pipeline output directory on subsequent runs. The local q3map2
recheck uses previously baked own-content BSPs and exported `.jkobs` files;
it regenerates train/validation/held-out JSONL with the current standalone tool,
runs `validate` and `audit-bsp` for every archive, then runs
`tools/rend2/datasets/verify_q3map2.py`. Source archives are hardlinked read-only
into a fresh report tree; previous witnesses and licensed stock assets are not
modified. Exact commands, input hashes and output paths are in the witness.
To recreate the original bakes and actual-SP exports instead, follow
[the q3map2 guide](rend2-q3map2-validation.md). The current [core](rend2-reconstruction-core.md),
[disk](rend2-reconstruction-disk.md), [lifecycle](rend2-reconstruction-lifecycle.md)
and [offline](rend2-offline-observation-datasets.md) documents specify the next
solver's boundaries. A solver must retain raw evidence/assumptions, produce
validated output explicitly, and demonstrate accuracy before a consumer can
claim recovered physical lighting.
