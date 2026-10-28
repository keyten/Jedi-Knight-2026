# Standalone reconstructed-lighting CPU tests

Run from the repository root (MSVC 2019 x64):

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-standalone -G "Visual Studio 16 2019" -A x64
cmake --build build/lightrecon-standalone --config Release --parallel 4
ctest --test-dir build/lightrecon-standalone -C Release --output-on-failure
```

On other platforms omit `-G ... -A x64`. CMake must be on PATH or invoked using
its absolute executable path. This project uses only C++11 and the standard
library: no OpenGL context, engine, SDL, Boost or game assets are required.

All executables link the one `rend2_lightrecon_core` static target:

- `lightrecon_test`: canonical asset and photometric contracts.
- `lightrecon_api_test`: read-only snapshots, queries and lifetime.
- `lightrecon_core_test`: streaming input, validation, sessions and host errors.
- `lightrecon_control_test`: build/load policy, independent consumer gates and SP/MP cvar registration.
- `lightrecon_disk_test`: portable output codec, corruption/limits and atomic cache I/O.
- `lightrecon_lifecycle_test`: world-scoped publication and failed/unloaded sessions.
- `lightrecon_lightmaps_test`: original bounded CPU image snapshots.
- `lightrecon_decode_test`: photometric candidates, censoring and renderer math parity.
- `lightrecon_lightgrid_test`: indexed JA grid evidence, coordinates/styles/HDR,
  missing/malformed data, lifetime and all 65,536 direction encodings against
  compiled current SP/MP entity math.
- `lightrecon_surfaces_test`: original lightmap UV correspondence, positions,
  separate normals, coverage/area, seams/overlaps, analytic patches and lifetime.
- `lightrecon_classification_test`: deluxe correspondence/direction quantization,
  distinct lightstyles/vertex RGBA/HDR, material categories and conservative eligibility.
- `lightrecon_visibility_test`: immutable CPU BVH, static surface/patch rays,
  optics/sky/domain uncertainty, independent reference, batches and concurrent readers.
- `lightrecon_regions_test`: adaptive free-space certificates, BSP domains,
  components/room candidates/apertures, moving uncertainty, grid and surface sides.
- `lightrecon_bake_forward_test`: historical direct kernels, explicit capability
  failures, factors/invalidation and the separate SI footprint integral.
- `lightrecon_bake_forward_tool`: geometry-only CPU protocol for external
  fixed-geometry tests; see [S01 support and reproduction](../../docs/rend2-bake-forward-evaluator.md).

`lightrecon_regions_inspect` is an optional offline JA RBSP v1 inspector and OBJ/TSV exporter.
See [lighting-region semantics, stock inspection and commands](../../docs/rend2-lighting-regions.md).

See [static visibility contracts, benchmark and test commands](../../docs/rend2-static-visibility.md).

See [classification contracts, map counts and test commands](../../docs/rend2-observation-classification.md).

See [luxel mapping, limitations, numerical and actual-SP validation](../../docs/rend2-luxel-observations.md).

See [lightgrid layout, conventions, memory and runtime validation](../../docs/rend2-lightgrid-observations.md).

See [core architecture and complete build commands](../../docs/rend2-reconstruction-core.md).
The obsolete lightgrid-source solver was removed. The core still has no inverse
solver; numerical forward prediction does not reconstruct or publish lights.
