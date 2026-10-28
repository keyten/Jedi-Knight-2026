# Inverse-lighting evaluation tools

[Metric definitions and evaluation policy](../../../docs/rend2-lighting-evaluation.md).
Python 3.12 standard library only. No solver, game, GL, new package or renderer
change. The code consumes frozen predictions under a **trusted controller**.

For non-vacuous positive-light, occlusion, encoding and profile gates, use the
[X05 acceptance controller](x05/README.md) and its [versioned witness](x05/results/README.md).
The historical dark/weak held-out folds below remain diagnostic evidence; they
are not positive-light capability tests.

## Self-contained run

From the repository root, using a fresh output directory:

```powershell
python -X utf8 -m unittest discover -s tools/rend2/evaluation -p 'test_*.py' -v
python -X utf8 tools/rend2/evaluation/benchmark.py --output build/evaluation-example
```

Outputs: `metrics.json`, `metrics.csv`, a ten-section `dashboard.md` and separate
`measurement.json`. Semantic reports have no timestamps/timing noise. A valid
empty asset, Candidate records, many weak lights and an indirect-only model do
not become successful physical reconstructions because their RGB happens to fit.

`examples/controller_case.json` is **evaluator-only** truth and metric test data,
based on the repository's existing analytical fixture geometry. It is not a
solver input. The perfect predictions are sanity controls, not fitted outputs.

## Existing analytical, compiled and licensed stock data

This change reuses already generated captures; it does not rebake q3map2 or start
the game. Data-generation instructions remain in [datasets](../datasets/README.md).
Local paths used for the complete evaluation run:

```powershell
python -X utf8 tools/rend2/datasets/measure.py build/lighting-evaluation/run-memory.json python -X utf8 tools/rend2/evaluation/benchmark.py --output build/lighting-evaluation/new-full --analytical-answers build/offline-suite-msvc-accepted/answers --compiled-root build/x02/compiled-fixtures-frozen --compiled-answers build/q3map2-bake-verified/answers --stock-root build/x02/final
```

Omit optional arguments if those local datasets are absent; an omitted stage is
not a passed check. The runner scans 11 analytical answers, 12 compiled datasets
and all original colour pages for `hoth3`, `hoth2`, `mp/ffa3` when provided. It
reports unknown full-world area, source units and per-ray geometry certification
explicitly. Compiled held-out RGB baselines are encoded-byte-domain zero and
target-copy assertions, not source-linear prediction accuracy of a fitted solver.
Analytical source-copy assertions test matching, not inverse recovery.

The default run also re-scores existing X03 conditional profile controls and all
35 original X04 boundary texels. It does not run their optimizers or compiler.

Reduce a completed run into compact versionable evidence without stock pixels or
geometry:

```powershell
python -X utf8 tools/rend2/evaluation/summarize.py --run build/lighting-evaluation/new-full/metrics.json --measurement build/lighting-evaluation/run-memory.json --output build/lighting-evaluation/compact
```

The checked-in [witness](results/README.md) and [JSON](results/summary.json) describe
the actual completed run, including its path/command, OS peak working set and
limits. Full geographic/page/material distributions remain in the local JSON/CSV.

The final run used `build/lighting-evaluation/complete/`, measured **176.587 s**
and **193,421,312 bytes** peak working set (184.46 MiB) for the evaluator. All
39 new tests, six existing blind-view tests, 11 X03 tests, 17 CPU CTest targets
and 12 compiled public-archive checks passed. Repeated example JSON/CSV bytes
were identical; manifest ingestion and frozen-regression comparison passed.
Exact test commands, hashes and unperformed stages are in
[validation.json](results/validation.json).

## Frozen future-solver predictions

The controller manifest schema is `org.openjk.lighting-evaluation-manifest.v1`:

```json
{
  "schema":"org.openjk.lighting-evaluation-manifest.v1",
  "cases":[{
    "truth":"private-case.json", "truth_sha256":"<sha256>",
    "prediction":"frozen-prediction.json", "prediction_sha256":"<sha256>"
  }]
}
```

Each private case follows `examples/controller_case.json`. Include immutable
provenance: BSP, geometry/decoder/mapping/calibration versions, metric/source
version, partition/threshold hashes, seed, resource limits and allowed profile
family. Do not use a placeholder map identity for real data.

Prediction files contain:

```json
{
  "map_sha256":"<same BSP content hash>",
  "outputs":{
    "predictions":[],
    "surface_predictions":[],
    "grid_predictions":[],
    "solver_counters":{}
  }
}
```

`outputs` is an allowlist; attempted truth/policy/domain/metadata overrides fail.
Prediction identities need not match truth IDs. Observation prediction IDs must
match the stable evaluation sample identities. At the freeze point, the trusted
adapter exports canonical read-only source/field records to this small JSON
projection; no canonical disk format or GPU data layout changes are needed.
There is no arbitrary canonical asset-cache binary Python binding in this change.

Supported source projection:

- `kind`: point, spot, rectangle, tube, sun, sky or transport.
- `state`: Resolved (default for evaluator fixtures), Candidate or Rejected.
  Only Resolved physical types count as recovered emitters.
- `position`: world XYZ for finite physical sources.
- `direction` with `direction_convention`: propagation for spot/rectangle,
  receiver_to_source for sun, axis for tube.
- `dimensions`: full width/height or full tube length/radius; optional tangent,
  two-sidedness and spot half-cone degrees.
- `photometry`: Y value, unit, shared domain/calibration identity, calibration kind,
  RGB and primaries. The canonical Y=1 colour convention is preserved by explicit
  normalization; RGB chromaticity comparison also accepts equivalent positive RGB
  scaling. Unknown values are omitted/null, not assumed calibrated.
- Optional `existence`: score, meaning=probability and a calibration_protocol
  identifying the probability claim being evaluated. Canonical heuristic
  confidence does not automatically map to this field.

Surface/grid target streams specify positive weights, exact comparison_id, domain,
primaries, lower/upper RGB intervals (upper null means censored) and optional
region/lighting/brightness categories. Weights are covered surface area for surface
streams; grid volume/equal-record sampling is explicitly separate. Predictions
provide id, comparison_id, domain, primaries and RGB. The metric library performs
no colour transfer, gain fitting, albedo division or new source inference.

Evaluate only after predictions have been frozen:

```powershell
python -X utf8 tools/rend2/evaluation/benchmark.py --manifest private/benchmark.json --output build/evaluation-frozen
```

For optional performance fields, counters are optimization_seconds, peak_rss_bytes,
trace_count, iterations, function_evaluations and result_bytes. Missing counters
are Unavailable; evaluator runtime is not optimizer runtime. Treat counters as
solver claims until verified by the external controlling process.

## Blind boundary and negative tests

Use X02 `prepare_blind.py` and `TrainingView`, not a full archive plus partition
tags. The controller alone holds `EvaluationView` and source answers. The new
`audit_training()` rejects extra files, hidden recipe/answer metadata, wrong map,
role, schema and corrupt members. No private backing store is loaded into the
solver view. Tests also check held-out stage permissions, filtered spatial gaps,
cross-map predictions and attempted truth override.

Existing shared-record/alternative-image checks:

```powershell
python -X utf8 -m unittest discover -s tools/rend2/datasets -p 'test_blind_views.py' -v
```

The actual 12 compiled solver geometry archives were separately checked:

```powershell
Get-ChildItem build/x02/compiled-fixtures-frozen -Directory -Filter '*-views' | ForEach-Object {
  & build/lightrecon-core-renderers/Release/lightrecon_dataset_quality.exe public-check ($_.FullName + '/inputs/geometry.jkobs') unused
  if ($LASTEXITCODE -ne 0) { throw 'Public archive sanitization failed' }
}
```

This checks the API/artifact boundary, **not** a hostile executable's OS rights.
Mount only read-only `inputs/` for an untrusted solver, using a separate sandbox/
account/machine. Do not mount answers, full archives, recipes, these controller
fixtures or evaluation output. A Python property or working directory cannot
block arbitrary filesystem access; no platform sandbox is installed or claimed.

## Regression and ablations

Predeclare an independent JSON policy, for example:

```json
{"rules":[
  {"path":"/cases/0/sections/source_accuracy/recall", "direction":"higher", "allowed_delta":0},
  {"path":"/cases/0/sections/geometric_visibility/rays/wall_leaked_energy", "direction":"lower", "allowed_delta":0}
]}
```

```powershell
python -X utf8 tools/rend2/evaluation/benchmark.py --manifest private/benchmark.json --previous previous/metrics.json --regression-policy private/frozen-policy.json --output build/evaluation-comparison
```

Metric paths refer to the flattened report, including stable manifest case order.
Unavailable required paths fail rather than become zero. Diagnostic example gates
are not stock-map pass thresholds. Freeze physical resolution/error budgets before
final solver tuning. Compare paired scenes at fixed coverage and inspect source
FP/recall, wall leaks, attribution and calibration alongside RGB errors.

`ablation_pairs()` pairs supplied completed runs only if dataset/split/policy hashes,
seed and resource budget agree and exactly one of lightgrid, materials, visibility,
shadows, indirect or emissive_priors changes. No ablation fitting is implemented.

## Regression tests executed

```powershell
python -X utf8 tools/rend2/bake_model/test_identifiability.py -v
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure
```

Local Python:
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`
(3.12.14). For ctest, prepend
`C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin` to PATH.
No shared production sources changed, so no new SP/MP build, GPU or gameplay
validation is claimed. See the witness/specification for remaining uncertainty;
passing these fixtures cannot establish general stock reconstruction readiness.
