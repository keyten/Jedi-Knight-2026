# FixedGeometryBlindIntensityFit (X07)

An offline, test-only RGB amplitude optimizer that calls the **production C++**
`Bake::CpuForwardEvaluator` for every prediction, finite-difference sample and
strict final replay. Python supplies orchestration and small numerical routines;
there is no Python lighting formula in this worker. Standard library only,
GPL-2.0-or-later; no SP/MP runtime dependency is added.

Read the [technical report](../../../docs/rend2-fixed-geometry-blind-fit-x07.md)
for measured errors, isolation scope, uncertainty and unsupported cases.
Supplied point XYZ/count/type and a selected controlled compiler profile are
test priors. This does not discover original lights or calibrate candela.

From the repository root (PowerShell; use fresh output directories):

```powershell
cmake -S tools/lightrecon_test -B build/x07-tools -G "Visual Studio 16 2019" -A x64
cmake --build build/x07-tools --config Release --parallel 6
$bin = 'build/x07-tools/Release'
python -X utf8 tools/rend2/fixed_geometry/run.py --output build/x07-run-a --exe "$bin/lightrecon_bake_forward_tool.exe" --quality "$bin/lightrecon_dataset_quality.exe" --dataset-tool "$bin/lightrecon_dataset_tool.exe"
python -X utf8 tools/rend2/fixed_geometry/run.py --output build/x07-run-b --exe "$bin/lightrecon_bake_forward_tool.exe" --quality "$bin/lightrecon_dataset_quality.exe" --dataset-tool "$bin/lightrecon_dataset_tool.exe"
$env:JK_X07_FIXTURE_ROOT = 'build/x07-run-a'
python -X utf8 -m unittest discover -s tools/rend2/fixed_geometry -p 'test_*.py' -v
python -X utf8 tools/rend2/fixed_geometry/report.py --run build/x07-run-a --compare build/x07-run-b --exe "$bin/lightrecon_bake_forward_tool.exe" --output build/x07-report
```

Single-configuration builds omit `Release/`; Unix omits `.exe`. On non-Windows,
RSS is explicitly unavailable rather than a fabricated zero. Tests needing the
adapter/compiled inputs explicitly skip if absent. `run.py` requires and checks
these existing, ignored artifacts by default:

- `build/q3map2-bake-verified/authoring/base/maps/*.bsp` and `.map`;
- `build/q3map2-bake-verified/answers/*.q3map-recipe.json` (controller only);
- `build/x02/compiled-fixtures-final/{point_room,two_colour,door_occlusion}`
  with `.jkobs`, `.public.jkobs`, `.rows.jsonl`, `.source.json`.

Use `--bakes` and `--datasets` for alternate existing locations. To recreate
missing compiler/capture artifacts, follow the pinned
[q3map2 workflow](../../../docs/rend2-q3map2-validation.md), then
`datasets/test_quality_pipeline.py --quality QUALITY --tool TOOL --fixtures CAPTURED_INPUTS --output NEW_DERIVED`.
This compiles real maps, captures their original CPU observations and derives
geometry. Compiler whole-BSP repeatability is not assumed: X07 refuses changed
BSP/map hashes against the frozen q3map2 witness. A genuinely different bake
requires a separately reviewed witness, not bypassing the hash checks.

Only mount a model's `inputs/` and writable `work/` for a blind worker. The
controller owns `private/`, `views/evaluator/` and final metrics. The Python
audit guard additionally denies actual hidden-file reads and process/native
escapes in this trusted prototype. It is **not an OS security sandbox against
malicious native code**. An untrusted external solver needs a separate OS
user/container with only those mounted inputs and the trusted adapter.

`results/validation.json` contains deterministic parameters, metrics, identity
hashes, work counts and negative controls. `measurements.json` keeps variable
time/RSS measurements separate. `report.py` compares two complete runs, removing
only named measurement fields and the full-file freeze hash (which includes
timing); it retains the frozen-parameter hash, profile/input hashes and all
numerical/work results. No game assets or observed held-out pixel arrays are
included in versioned reports.
