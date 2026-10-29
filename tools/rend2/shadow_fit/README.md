# S09 shadow/caster/source evidence

CPU-only automatic association and bounded source fitting, with explicit null,
alternative and unknown outcomes. See [the technical report](../../../docs/rend2-shadow-evidence-s09.md).

```powershell
cmake -S tools/lightrecon_test -B build/s09-tools -G "Visual Studio 16 2019" -A x64
cmake --build build/s09-tools --config Release --parallel 2
python -X utf8 tools/rend2/shadow_fit/benchmark.py --tool build/s09-tools/Release/lightrecon_shadow_test.exe --inspect build/s09-tools/Release/lightrecon_shadow_inspect.exe --out build/s09-new-run
python -X utf8 -m unittest discover -s tools/rend2/shadow_fit -p "test_*.py" -v
```

Single-configuration builds omit `Release/`. Output directory must be fresh.
Only Python's standard library is used; GPL-2.0-or-later. Existing compiled
fixture and archive prerequisites/overrides are listed in the technical report.
Absent/changed fixtures fail explicitly. No renderer or Python shadow-association
replacement is used for the fitting loop.

The source X experiment supplies height/Y and fixed source count. Three starts
and five A–E ablations are frozen before held-out evaluation. It is not arbitrary
XYZ recovery. `results/numerical.json` separates semantic results from
`results/resources.json`. The possible-edge confusion witness deliberately
includes indistinguishable undisclosed albedo; none is certified as a physical
shadow. Actual q3map2 archive diagnostics are training-only, with supplied XYZ,
and do not claim ground-truth shadow association accuracy or stock recovery.
