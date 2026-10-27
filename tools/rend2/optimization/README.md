# Numerical optimization benchmark (offline research only)

[Technical decision and equations](../../../docs/rend2-optimization-benchmark.md).
This compares **independent parameter blocks**, not a production inverse solver.
No asset is published, no new light is generated and no renderer feature changes.

## Dependencies

Python 3.12.14, NumPy 2.3.5 and SciPy 1.16.3. Versions are pinned in
`requirements.txt`; exact local optimizer wrapper/native and license hashes are
in [dependencies.json](dependencies.json). NumPy/SciPy use BSD-3-Clause licenses;
their wheels also contain third-party notices that must be preserved separately.
No library code, binary or game content is committed. These packages have **no
CMake, SP/MP, runtime renderer or shader dependency**.

Use a research environment with the pinned packages, or install into the ignored
target searched by `environment.py`:

```powershell
python -m pip install --only-binary=:all: --target build/optimization-deps -r tools/rend2/optimization/requirements.txt
```

In this local run NumPy was already installed; only SciPy was added with
`--no-deps --target build/optimization-deps scipy==1.16.3`. The original wheel was
not retained, so a verified wheel checksum is **not** claimed. The zero-byte
placeholder left in that directory is not an installable/verifiable wheel.

The local pip-created directories are readable by their owner but not the Codex
sandbox identity. Numerical commands were therefore executed under the package
owner's context. A proposed recursive ACL inheritance change was rejected by
automatic review; **no ACLs were changed**. No permissions change is required for
an ordinary owner-controlled Python environment. The benchmark performs no
network work after packages are installed.

## Tests and self-contained controls

Run from the repository root:

```powershell
python -X utf8 -m unittest discover -s tools/rend2/optimization -p 'test_*.py' -v
python -X utf8 tools/rend2/optimization/run.py --quick --output build/optimization-smoke
```

Use a fresh output directory. Without optional compiled arguments, kernel/geometry/
shadow/GI controls still run and missing compiler-backed stages are explicitly
reported. The source BSP conformance test skips when its pinned files are absent;
do not call that a passed compiler experiment.

The tests cover nonnegative solutions/KKT, rank deficiency, sparse bias, bounded
solvers, counted finite differences, cancellation/restart, nonfinite/invalid data,
correct truncation/censoring, shared-peak encoding, unknown profile/optics,
distance clamps/coincident receivers, analytic derivatives, shadow integration,
fixed-sample zero-gradient failure, strict refinement after a surrogate, spot
degeneracy, indirect collapse and discrete count selection.

## Full run with existing compiled observations

Existing q3map2 recipes, source locks and generation instructions remain in
[bake_model](../bake_model/README.md) and [datasets](../datasets/README.md).
This benchmark uses their unmodified, hash-checked BSP/capture results. It does
not silently download assets, rebake maps or start JA.

```powershell
python -X utf8 tools/rend2/datasets/measure.py build/optimization-run-memory.json python -X utf8 tools/rend2/optimization/run.py --output build/optimization-run --compiled22 build/bake-model-comparison-final --compiled12 build/x02/compiled-fixtures-frozen --recipes12 build/q3map2-bake-verified/answers --ray-tool build/lightrecon-core-renderers/Release/lightrecon_profile_rays.exe
```

The local Python executable actually used was
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.
`datasets/measure.py` records actual Windows process peak working set and wall
time. Other platforms should use an equivalent OS peak-RSS recorder; an absent
RSS measurement is not replaced with accounted array bytes.

The four original compiler cases are inverse_square, linear, spot and colour_clip.
Their scalar/grid/deluxe subset checks are rerun without invoking the compiler.
The additional two_colour and door_occlusion archives use actual CPU geometry ray
queries, with missing/unknown observations excluded explicitly and query work
recorded. Original compiler-relative intensity remains distinct from physical SI.

Full mode uses two seeds (11,37), noise levels (0,.5), two geometry starts, five
continuous optimizers and independent count/colour/geometry/transport controls.
Outlier tests additionally use seeds 11,37,71 and noise 0/.03. Defaults, bounds,
finite-difference steps and work limits are fixed in source and hashed per run.

## Outputs and determinism

- `result.json`: semantic results, every unsuccessful start, source/data hashes,
  exact package versions and missing-stage information.
- `metrics.csv`: per-run quality/status/work counters.
- `tables.md`, `summary.json`: per-problem/per-method summaries, including costs.
- `timings.json`, `measurement.json`: timing separated from semantic results.
- `rays/`: fixed-geometry query/response witnesses and actual CPU command argv.

Semantic JSON/CSV contains no wall times. Repeated runs with the same build,
CPU/library environment and datasets must be checked byte for byte. This is not
a promise of last-bit equality across BLAS/compiler/CPU versions. BLAS/OpenMP
thread counts are requested as one before imports; no background solver threads
or GPU work are introduced. Raw geometry-ray command paths are kept in run-local
files; semantic metadata normalizes only the output-directory prefix.

Changing code while a run is active invalidates it; start/end source hashes are
checked. Numerical statuses are independent of physical confidence. Budget or
cancellation can return diagnostic best-feasible parameters, not a successful
reconstructed-light asset.

The versioned [result tables](results/tables.md), [full scalar witness](results/result.json)
and [validation record](results/validation.json) preserve the completed run.
All scenes/observations used here are self-authored controlled fixtures; no stock
BSP, textures, screenshots or lightmap pages are redistributed.

The completed local full run is `build/optimization-benchmark/validated/`, repeated
in `validated-repeat/`: **620 method/start records**, **153.771 s**, **81,612,800
bytes** peak working set (77.83 MiB). The repeat took 135.766 s / 81,797,120 bytes;
wall time is environment-dependent, while semantic JSON/CSV matched byte for byte.
Thirty new tests, 39 evaluation tests, 27 X04 tests, 15 GI tests and all 17 existing
CPU CTest targets passed. No new SP/MP build or gameplay/GPU check is claimed.

An additional receiver-baseline ablation isolates height/intensity ambiguity that
the main multi-surface control can resolve:

```powershell
python -X utf8 tools/rend2/optimization/report/distance_control.py --output build/optimization-distance.json
```

Its 96 records compare a repeated point, a narrow plane, a broad plane and an
independent wall baseline; results are in [distance.json](results/distance.json).
This is a separate controlled experiment, not a hidden modification of the main
benchmark targets. It uses the same kernel and explicitly recorded source scale.

Per-method peak memory is also measured in **separate processes** on common small
linear/nonlinear objectives, alongside an import-only reference:

```powershell
python -X utf8 tools/rend2/optimization/report/resource_matrix.py --output build/optimization-resource-matrix
```

[resources.json](results/resources.json) records all 11 process measurements. Peaks
include interpreter/native-library imports; do not subtract independent peaks as
an exact allocator cost or extrapolate these small cases to full-map sparse work.

To publish completed scalar evidence after checking repeated semantic bytes and
current source hashes:

```powershell
python -X utf8 tools/rend2/optimization/report/publish_results.py --run build/optimization-benchmark/validated --repeat build/optimization-benchmark/validated-repeat --measurement build/optimization-benchmark/validated-measurement.json --repeat-measurement build/optimization-benchmark/validated-repeat-measurement.json --distance build/optimization-benchmark/distance.json --resources build/optimization-benchmark/resources/result.json --output tools/rend2/optimization/results
```

An early floor-only adapter selected no receivers from the existing wall-only
X02 folds for `two_colour`/`door_occlusion`. That output was discarded, the adapter
was corrected, a nonempty-fold/blocked-ray regression test was added, and both
final runs used real data (58/42/48 train/validation/held-out rows per scene).
The validation record also preserves a rejected run where code changed while
the run was active. Neither intermediate run is counted as a successful check.

## Interpreting the blocks

- A compares NNLS/BVLS/TRF, coordinate and group-prox on a genuinely linear active
  branch. Peak-censored rows are not silently fit as linear irradiance. Exact
  compiler predictions are reevaluated after fitting.
- B keeps profile families discrete and uses identical RGB nuisance bounds/starts.
- C separates generated smooth targets from actual compiled XYZ refinement.
- D separates reference, point, fixed grid, adaptive, polygon and surrogate training
  values. Every result is scored on the stricter reference. A two-stage surrogate
  then reference schedule shares the original total forward budget.
- E evaluates bounded source masks/types and sparsity; confidence is not residual.
- F separates passive source-linked transport from a deliberately flexible
  unresolved residual that suppresses the true source.

Reference solvers: SciPy NNLS/BVLS/TRF/L-BFGS-B/Powell. Projected damped GN, Adam,
coordinate and group-prox loops are small benchmark routines with conformance
tests, not replacements for maintained production optimization libraries. TRF/
NNLS iteration counts not exposed by the library are null; actual forward and
derivative work is separately counted. Dense library-internal BLAS matvec counts
are null, not invented zero-cost operations.

Validation must consider physical parameter error, strict held-out error,
information rank, failed starts, rays and CPU cost together. A perfect spot-core
RGB fit with the wrong cone remains unidentifiable, and an indirect-only fit does
not recover a lamp. The result is never translated into source-existence
probability or stock-map reconstruction readiness.
