# P01 native CPU field-builder witness

The native test calls `Fields::Build`, actual P02 visibility/topology, actual S10
passive fitting, P03 projection, canonical serialization and immutable Snapshot
queries. Python only freezes inputs, launches the test and measures resources.

From the repository root, with GCC/CMake on PATH:

```powershell
cmake --build build/s06-gcc -j 2
& C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -X utf8 tools/rend2/field_builder/run.py --out tools/rend2/field_builder/results/v2
```

Use a new output directory when reproducing; existing witnesses are not overwritten.
`first.jklr` and `repeat.jklr` are tiny owned example field assets with identical
bytes. No licensed game geometry is embedded. `protocol.json` freezes compiler,
native/source hashes, environment and policy before any measured run.
`summary.json`/`quality.csv` retain missing support separately from numerical error.
`checks.json` and hashed check logs record broader build/regression status.

The native suite contains deliberately poor coarse direct and bounce controls.
Those errors and missing direction coverage are diagnostics, not successful field
accuracy claims. Finer passive-bounce topology is measured on the same independent
positions. A default SH2 direct projection fails the unchanged angular-error gate.

[Detailed model, support matrix and limitations](../../../docs/rend2-lighting-field-builder-p01.md).
No stock physical field, historical Q-to-candela conversion or renderer consumer
is provided by this witness. No extra third-party numerical dependency is needed.
