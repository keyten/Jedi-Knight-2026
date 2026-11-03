# P05 native static visibility cache witness

Native C++ generates sparse static/source/sky support using the production visual
BVH and X13/X14 policies. Python only freezes/launches/measures it. No licensed data
or source-amplitude answer is needed. This is physical static geometry, not an
historical compiler trace replay or a renderer shadow-map bake.

```powershell
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc -j 2
& C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -X utf8 tools/rend2/shadow_cache/run.py --out tools/rend2/shadow_cache/results/v3
& C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -X utf8 tools/rend2/shadow_cache/reference.py --native tools/rend2/shadow_cache/results/v3/first.json --out tools/rend2/shadow_cache/results/v3/analytic-reference.json
ctest --test-dir build/s06-gcc --output-on-failure -j 2
```

Use a fresh output directory on reproduction. `protocol.json` pins code/compiler,
tool and predeclared numerical/work policy. `first.json`, `repeat.json` and
`summary.json` retain Unknown and sampling disagreement explicitly. Timings/RSS
are excluded from semantic comparison. `first.jklr`/`repeat.jklr` are identical
owned controlled canonical examples, with the sparse payload explicitly reloaded
and prepared by the test. `manifest.json` pins frozen files; separate check receipts
retain broader tests/builds, including any failures.

The tube case intentionally exceeds the quadrature-discrepancy threshold; its
flag is evidence to refine, not a failed exact-shadow confidence claim. Alpha,
unbounded static geometry, unknown doors and missing sky never become successful
darkness. Finite source visibility is not a physical source-existence certificate.

[Full contract and limitations](../../../docs/rend2-static-shadow-support-p05.md).
No new third-party dependencies; GPL-2.0-or-later and standard libraries.
