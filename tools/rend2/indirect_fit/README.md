# S10 passive one-bounce and unresolved transport

The production C++ implementation is `lightrecon/indirect.{h,cpp}`. It keeps
fixed direct/local, sun, sky and identified ambient roots separate. Source-linked
one-bounce energy has no independent per-luxel gain. A bounded coarse appearance
field is optional and explicitly unresolved. No renderer consumer is involved.

```powershell
cmake -S tools/lightrecon_test -B build/s10-tools -G "Visual Studio 16 2019" -A x64
cmake --build build/s10-tools --config Release --parallel 2
python -X utf8 tools/rend2/indirect_fit/benchmark.py --tool build/s10-tools/Release/lightrecon_indirect_test.exe --out build/s10-repeat
python -X utf8 -m unittest discover -s tools/rend2/indirect_fit -p "test_*.py" -v
```

The output directory must be new. Single-configuration builds omit `Release/`.
Only Python's standard library is needed. The benchmark requires the existing
hash-pinned `build/inverse-gi-controls/` paired bakes; regenerate them with the
workflow in `tools/rend2/inverse_gi/README.md` and its compiler source locks.
The comparison reuses compiled bytes, rather than claiming a new compiler run.
It cannot identify the stock Raven compiler or establish exact physical GI.

`numerical.json` contains deterministic outputs and original compiler checksums;
`resources.json` keeps actual peak RSS/timing separate. C++ writes decomposition
TSV, named baseline/geometry diagnostics and canonical-field round-trip checks.
No licensed JA map or texture is included.

Canonical asset schema **2** adds explicit transport membership and unresolved
irradiance. Schema-1 assets/cache keys are rejected, not silently reinterpreted.
See [the implementation report](../../../docs/rend2-indirect-fitting-s10.md) for
equations, payload layout, assumptions, supported domains and remaining limits.
