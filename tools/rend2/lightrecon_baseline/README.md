# Disabled reconstructed-lighting baseline, schema 1

The original pre-cleanup baseline at
`60d1cc8c6f59e29e8d3d1ccc4b180a3369b7be2c` is retained in `obsolete-grid-v1/`.
The current baseline includes the reviewed deletion of obsolete source-fitting
GPU paths, based on `380ee0f7e`. The exact old/new diff and review summary are
archived alongside the old contracts. Twenty-two programs and all active GPU
interfaces were unchanged; five injection stages lost unreachable portal,
moment and retired debug code. See [the removal report](../../../docs/rend2-grid-reconstruction-removal.md).

- `matrix.json`: 27 representative configurations, with explicit technology
  settings and shader defines. Forward+ is a runtime uniform switch in lightall;
  the off/on rows deliberately share shader sources. Its compute kernels are
  separate rows with their existing SSBO interfaces.
- `preprocessed.json.gz`: complete normalized, preprocessed GLSL for 51 stages,
  plus macros, declared samplers, UBO/SSBO layouts, token and texture-call counts.
  Deterministic gzip (`mtime=0`) keeps derived GPL renderer source compact.
- `bindings.json`: relevant production sampler assignment expressions, numeric
  texture units, block slots and CPU buffer layouts. Retired moment samplers
  and bindings no longer exist in production.
- `manifest.json`: baseline revision/review reason and Intel driver reflection
  (active sampler names/types/counts, UBO sizes, SSBO bindings/sizes).
  Program IDs and driver-assigned locations are not part of the contract.
- `legacy_fx.efx`: newly authored deterministic FX sprite/light stimulus. It
  references a stock saber-flare material; it contains no commercial asset data.
- `validation.json`: historical pre-removal results and capture hashes. Large raw
  captures, installed PK3s, caches and execution logs stay under ignored `build/`.
- `lifecycle-validation.json`: subsequent main-world lifecycle CPU/runtime witness;
  this does not replace or regenerate the controlled GLSL goldens.
- `lightgrid-validation.json`: original record/index/HDR hashes and counts from
  stock-map SP lifecycle tests, including observation-only HDR with `r_hdr=0`.
- `luxel-validation.json`: source-geometry error/normal/coverage checks, exact
  atlas-independent export hashes and explicit SP overlay capture hashes. Raw
  screenshots and geometry exports remain under ignored `build/`; reproduction
  commands and limitations are in `docs/rend2-luxel-observations.md`.
- `classification-validation.json`: CPU/style/material/deluxe classification
  witness, stock hoth3/hoth2 counts, synthetic external deluxe-pair pixel checks
  and exact controlled SP before/after frame hashes. It does not replace the GLSL
  goldens or claim stock maps contain deluxe data. Source SHA256 values normalize
  CRLF to LF; DLLs, images and result files use exact byte hashes. Reproduction
  commands and runtime limitations are in `docs/rend2-observation-classification.md`.
- `visibility-validation.json`: synthetic CPU BVH correctness, independent
  triangle-reference errors, batch throughput/memory, concurrency, SP/MP builds
  and unchanged preprocessed shader contracts. It contains no map/game assets;
  commands and optical/geometry limitations are in `docs/rend2-static-visibility.md`.
- `regions-validation.json`: adaptive spatial-graph tests, SP/MP builds,
  bounded hoth3 offline statistics, repeated/cross-compiler OBJ/TSV identity and
  debug-slice hashes. Large source maps/exports remain under ignored `build/`;
  reproduction and limitations are in `docs/rend2-lighting-regions.md`.
- `grid-removal-inventory.json`: pre-edit dependency search, declarations,
  private state and caller references for the deleted experimental subsystem.
- `grid-removal-validation.json`: SP/MP build, CPU/GPU and actual-SP removal
  witness; nine fixed stock-map views, lifecycle/raw-input checks and a separate
  self-authored saber/NPC fixture. Failed initial attempts and limits are explicit.
  It records per-run DLL hashes rather than claiming every intermediate capture
  used the final binary. See `docs/rend2-grid-reconstruction-removal.md` for commands.
- `series-audit-validation.json`: review of all eighteen infrastructure commits,
  the unknown-colour-basis comparison fix, independent-consumer/candidate tests,
  fresh dual-compiler CPU/shader checks and offline replay of existing q3map2
  datasets. It distinguishes new checks from historical gameplay/bake evidence
  and records the unresolved intermittent atomic-replacement failure.

Run the existing suite's focused selection:

```powershell
python tools/rend2/run_gpu_tests.py --only '*lightrecon*'
python tools/rend2/run_gpu_tests.py --only '*lightrecon*' --nvidia
python tools/rend2/lightrecon_regression.py --export-baseline build/lightrecon-baseline-readable
```

The preprocessor is GCC/Clang (`REND2_CPP` or `--cpp`). Compilation uses the
existing hidden SDL context and `test_watersurface_gl.compile_program`, not a new
shader compiler. `--no-gpu` is a contract-only check and explicitly reports that
compilation was not run. Missing compiler/context/assets are errors.

Changing unrelated renderer behaviour does not require preserving an old byte
stream: comments, whitespace and source line numbers are ignored. Review real
differences per stage/configuration in `build/lightrecon-regression/comparison.diff`.
If an unrelated change is intended, document it and explicitly update the
controlled baseline with `--write-baseline 'specific review rationale'`. Do not
automatically bless the output in CI. Disabled branches are tested by mutations;
retired production files, symbols and controls have an explicit absence audit.

The production dependency fence currently asserts that canonical asset controls
are confined to their CPU owner/adapter. Before integrating a future consumer,
extend its disabled/enabled test cases and then deliberately update this fence.
Do not replace the fence with a broad file whitelist. The matrix represents a
controlled subset of production GLSL/header/library combinations, not every
possible material permutation or a dump of every program from an actual map.

See [the full audit, runtime protocol, tolerances and commands](../../../docs/rend2-reconstruction-regression.md).
