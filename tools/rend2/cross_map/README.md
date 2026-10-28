# X06 cross-map falsification audit

[Technical report](../../../docs/rend2-inverse-lighting-cross-map-validation.md),
[frozen decisions](baseline.json), [policy](policy.json), [results](results/README.md).
These programs orchestrate existing tools and small own-content controls. Python
standard library only; no new library, solver, renderer or field implementation.

Run from the repository root with Python 3.12. Actual local commands use:

```powershell
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$private=Join-Path $env:TEMP 'jk-lightrecon-x06-new'
New-Item -ItemType Directory -Path $private
$q3='build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe'
```

Licensed payloads must stay outside the Git checkout. Do not commit `assets*.pk3`,
BSP, rows/grid JSONL, original images or full debug geometry. Aggregate count SVGs
and hashes in `results/` contain no stock textures or lightmap samples.

## Freeze and inventory

`baseline.json` was recorded before new observations: HEAD, branch, existing X05
working changes, source/manifest hashes and decisions A–D. `policy.json` records
map priority, split, selection and budgets. Preserve both; create a new version
for changed criteria. Do not alter X05 thresholds to admit a failed control.

```powershell
& $py -X utf8 tools/rend2/cross_map/inventory.py --archives build/lightrecon-evidence-runtime/final-source/stock-assets/base --private $private --output local-inventory.json --hashes local-archive-hashes.json
```

The checked-in `availability.json` is the actual frozen local inventory. The
runner reads that file; for another installation, produce a reviewed copy with
the appropriate paths/private root before capture. Archive precedence is
lexicographic assets0..3: ctf1 comes from assets3. This is not a fallback to the
unpatched source when a later stage fails.

## Actual renderer capture and full-map census

`capture.py` reuses `prepare_runtime` and the actual `r_lightReconExport` command.
It uses known non-placeholder engine/game binaries from `build/x02/runtime/run`
and the current SP Rend2 DLL. It creates a private hardlinked stock-only base,
launches the child hidden and terminates only that child on its declared budget.
Reconstruction consumption remains master-off; no consumer is enabled.

```powershell
& $py -X utf8 tools/rend2/cross_map/capture.py --private $private
& $py -X utf8 tools/rend2/datasets/measure.py build/x06-new-census-memory.json $py -X utf8 tools/rend2/cross_map/census.py --private $private
```

The census reuses `audit-source-bsp`, `validate`, `scan`, `facts`, `public-check`,
`prepare_blind` and `metrics.readiness`. Complete raw/derived coverage is checked;
no two-page prefix is called all-pages. Output includes source identities, explicit
unknown transfer/primaries, count and area denominators, materials/styles,
grid/deluxe status, blind folds, stage measurements and count heatmaps. It resumes
completed records without hiding failed attempts. No OS hard memory sandbox is
installed: child peaks/time are polled against limits; parent RSS is measured.

The actual ctf1 failure was additionally tested with:

```powershell
& $py -X utf8 tools/rend2/cross_map/capture.py --private $private --maps mp/ctf1 --mapped-images 1 --tag=-prefix
& $py -X utf8 tools/rend2/cross_map/capture.py --private $private --maps mp/ctf1 --mapped-images 0 --tag=-small-roi --roi -4608 0 24 16
```

All three fail with `Section chunk limit`. They are retained as failures. The
second/third attempts are diagnostics, not permission to ignore missing full-map
coverage. The compact witness contains a separately labelled original-BSP-only
census for ctf1; it does not fabricate material or observation data.

## Bounded geometry and training-only candidate trials

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake --build build/lightrecon-disk-standalone --target lightrecon_regions_audit --parallel 2
& $py -X utf8 tools/rend2/cross_map/stress.py --private $private
& $py -X utf8 tools/rend2/cross_map/stress.py --private $private --regression
& $py -X utf8 tools/rend2/datasets/measure.py build/x06-new-stock-memory.json $py -X utf8 tools/rend2/cross_map/stock_trial.py --private $private
```

Stress selects four new ROIs/map and one per frozen regression map from training
geometry, then runs the existing matched-input replay + independent planar-ray
audit. OBJ/cells/rays stay private. Upward/fixture labels are selection intentions,
not certified outdoor/room/doorway semantics.

Stock trial reads only outer `TrainingView` RGB. Its metadata projection takes
only requested training IDs' footprint/kind/shading-normal values. Outer evaluation
targets are not opened. It tries at most eight fixture neighbourhoods, 48 samples
per nested role and eight fixed centroid-offset hypotheses per ROI. It ranks no
profile if common positive multi-normal/spatial support is unavailable.

Repeat exactly with `--output $private/stock-trials-repeat`. Compare results after
removing only `query_measurement` timing/path fields; IDs, choices, states and
admission must agree. This checks deterministic frozen-input analysis, not whole
q3map2 BSP compilation determinism.

## New controlled bakes, field checks and metrics

```powershell
& $py -X utf8 tools/rend2/datasets/measure.py build/x06-new-compiler-memory.json $py -X utf8 tools/rend2/cross_map/controlled.py --compiler $q3 --output build/x06-new-controlled
& $py -X utf8 tools/rend2/cross_map/field_checks.py --output build/x06-new-fields.json
& $py -X utf8 tools/rend2/cross_map/evaluation_checks.py --work build/x06-new-controlled --sky build/x06-new-controlled --output build/x06-new-evaluation
```

Controlled output is self-authored. `answers/` and original BSPs remain private
to the controller; `evaluation_checks.py` publishes physically separate sanitized
`views/inputs/` and `views/evaluator/` packages. Supply only read-only `inputs/`
to a future untrusted solver, in a separate OS access boundary. Do not mount the
whole result tree or its recipes. Current fits are deliberately **known-source
forward** nuisance experiments, not inverse recovery.

Ten default bakes cover coloured multi-light, bounce/material changes, oblique
spot, output gamma/clipping, saturation, thin wall/door, positive sky and co-located
source ambiguity. The earlier invalid `sky_offset` attempt remains under
`build/x06-controlled`; its missing shader is diagnosed and the corrected scene
has a new name. Optional `--names` runs selected controls only. All recipes retain
full argv and map/shader/texture/executable/BSP hashes. An unchanged lightmap does
not justify reusing a different whole-BSP hash.

Field checks reuse exact CPU support, passive transfer and SH/SG maths with new
inputs. They demonstrate unavailable support, uncertain optics, doorway support,
diffuse versus incoming-radiance bandwidth and smooth-branch derivatives. They
do not infer a stock field or port DDGI/RBDOOM GPU code.

## Tests and aggregate report

```powershell
& $py -X utf8 -m unittest discover -s tools/rend2/cross_map -p 'test_*.py' -v
$env:X05_RUN='build/x05/release'
& $py -X utf8 -m unittest discover -s tools/rend2/evaluation/x05 -p 'test_*.py' -v
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
& $py -X utf8 tools/rend2/lightrecon_regression.py --cpp C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/gcc.exe --no-gpu --output build/x06-new-disabled
```

Optional integration tests skip absent licensed/compiled local inputs explicitly.
The executed run had no skips. Existing optimization tests need their already
pinned SciPy environment; no full stock optimizer benchmark is requested.

`report.py` consumes the actual recorded local files. Default auxiliary paths
describe this witness. For a fresh run, pass `--fields`, `--census-measurement`,
`--compiler-measurement` and `--invalid-sky` explicitly; the latter is a controller
result JSON and may omit `sky_offset` if that failed authoring experiment was not
repeated. Missing required auxiliary files fail instead of becoming a passed stage.

```powershell
& $py -X utf8 tools/rend2/cross_map/report.py --private $private --controlled build/x06-controlled-final --evaluation build/x06-evaluation-v2 --output build/x06-new-compact
```

The final aggregate includes a post-freeze controller-only brightness census of
all folds. Those hidden RGBs were never used to select or fit stock trials.
`results/validation.json` gives exact actual commands, observed failures, tests,
resource measurements and unsupported stages. No proprietary input or claimed
physical stock source truth belongs in the versioned result.
