# X12 native stock-fit execution

[Report](../../../docs/rend2-stock-joint-fits-x12.md),
[frozen six-map protocol](results/v1/protocol.json),
[geometry-budget correction](results/v2/protocol.json),
[stage CSV](results/v1/stages.csv), [checks](results/v1/checks.json).
Python standard library only. Every numerical fit uses production C++.

The result is **zero accepted stock source models**. The successful compiled
controls use supplied XYZ, not source discovery. Missing stock heldout
predictions have null error; they never receive a successful black score.

Run from the repository root. Actual local paths:

```powershell
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
$private=Join-Path $env:TEMP 'jk-lightrecon-all-stock-20261031'
cmake --build build/s06-gcc --parallel 2
```

The existing private root owns `inventory.json`, `captures.json`, original
all-pages archives and frozen native executables. It contains licensed files
and must not be committed or mounted into a blind solver environment.
Inventory hashes match the preserved 57-map v4 audit. No game is required.

## Exact original attempts

The original v1 executable is preserved outside Git as
`$private/runs-x12-a-native.exe` (hash in results). It uses 64 coarse rows.
Current source includes the later geometry-derived minimum. Do not call a new
build an identical v1 experiment. These commands reproduce locally with the
original frozen binary, choosing fresh run names:

```powershell
& $py -X utf8 tools/rend2/stock_joint_fits/run.py --private $private --tool "$private/runs-x12-a-native.exe" --revision x12-v1-new --protocol tools/rend2/stock_joint_fits/results/v1/protocol.json
& $py -X utf8 tools/rend2/stock_joint_fits/run.py --private $private --tool "$private/runs-x12-a-native.exe" --revision x12-v1-new-repeat --maps mp/duel4 t1_danger hoth3
```

Original run names: `x12-a`, `x12-repeat`, followed by `x12-b` (coarse correction)
and `x12-c` (same correction, precise forward capability diagnostics). No result
or old v4 witness is overwritten. `x12-b`'s generic capability failure is retained
under `results/v2/initial.json`. `x12-c` identifies the unsupported shading-normal
sample. Frozen settings are common to both conditional profiles.

For a fresh all-six experiment from current source, freeze a **new** protocol
first; current `freeze.py` declares the corrected coarse policy:

```powershell
& $py -X utf8 tools/rend2/stock_joint_fits/freeze.py --private $private --out build/x12-new-policy.json
& $py -X utf8 tools/rend2/stock_joint_fits/run.py --private $private --protocol build/x12-new-policy.json --revision x12-new
```

Reproduce the scoped academy1 follow-up with current source:

```powershell
& $py -X utf8 tools/rend2/stock_joint_fits/run.py --private $private --protocol tools/rend2/stock_joint_fits/results/v2/protocol.json --revision x12-academy-new
```

The runner fingerprints all datasets and copies the native executable before
starting children. Each map's outer limit is 600 seconds/1 GiB measured process
peak working set. Deterministic internal work budgets are separate from timers.
The scripts refuse an existing run name and detect a coarse-policy mismatch.

## Independent compiler and native regressions

These commands use existing captured compiler fixtures and separated blind
views. They do not execute a compiler rebake, discover XYZ or estimate SI units.

```powershell
& $py -X utf8 tools/rend2/producer/benchmark.py --tool build/s06-gcc/lightrecon_joint_tool.exe --out build/x12-compiled-new
& $py -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/s06-gcc/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/x12-forward-new
$env:S11_CASE='dynamic-scope'
& build/s06-gcc/lightrecon_joint_test.exe build/x12-dynamic-scope-new.json
Remove-Item Env:S11_CASE
ctest --test-dir build/s06-gcc --output-on-failure -j2
$env:CL='/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

Actual results: 35/35 CPU tests pass (290.89 seconds); both Release renderer
targets pass. Python: seven blind-view, six production-forward, four historical
audit, two scheduler-report and four X12-report tests pass (23 total). No test
skip is accepted by `finalize.py`. The first build-wrapper invocation lacked
cmake in PATH and an initial test pattern found zero tests; corrected invocations
are the ones counted. Neither is hidden as a pass.

Disabled renderer checks:

```powershell
& $py -X utf8 tools/rend2/test_lightrecon_legacy_gl.py --output build/x12-disabled-new
& $py -X utf8 tools/rend2/producer/compare_revision.py --revision a83d663ff --gpu-report build/x12-disabled-new/report.json --out build/x12-disabled-vs-input-new
```

The first command intentionally still fails the old historical baseline with
3,870 diff lines; all 27 shader programs compile/link on the current GPU. The
input-revision comparison passes with zero source/binding differences. No
baseline is rewritten, no ordinary loading invokes the producer, and no runtime
light consumer is enabled.

## Collect, verify and interpret

```powershell
& $py -X utf8 tools/rend2/stock_joint_fits/collect.py --private $private --revision x12-a --repeat x12-repeat --out build/x12-stock-new.json
& $py -X utf8 -m unittest discover -s tools/rend2/stock_joint_fits -p test_reports.py -v
```

`collect.py` preserves errors, null metrics and provider/coverage limitations.
The collector removes stock coordinates/target pixels from public results;
only aggregate counts, stable IDs and private debug hashes remain. Full unit
bounds, source-placement TSVs, ray witnesses, topology OBJ and mapping data stay
private. No original-versus-predicted images exist for rejected models.

`report.py` deterministically generates the checked-in count SVG/CSV from the
results. `finalize.py` seals the known-compiler results, actual build/CPU records,
Python tests and artifact hashes after those commands have run. OS time/RSS stay
outside deterministic numerical witnesses. Its default paths intentionally name
the preserved X12 execution, rather than silently inferring missing checks.

There is no accepted stock model to compare against ambient, wrong XYZ/type,
source removal or alternative GI on final heldout. Those comparisons are marked
unperformed, not assigned zero error. A larger independently compiled blind
discovery bridge also remains unperformed. Fix the documented motion/support/
admission blockers before claiming original JA source reconstruction.
