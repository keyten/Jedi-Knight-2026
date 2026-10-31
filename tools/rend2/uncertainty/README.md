# S12 uncertainty benchmark

CPU C++11 production implementation, Python standard-library controller. No new
third-party dependencies; project GPL-2.0-or-later. See
[the technical report](../../../docs/rend2-uncertainty-s12.md).

From the repository root (PowerShell, adjust toolchain paths for your machine):

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s12 -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build build/s12 --target lightrecon_uncertainty_test lightrecon_uncertainty_population --parallel 2
python -m unittest tools.rend2.uncertainty.test_benchmark -v
python tools/rend2/uncertainty/benchmark.py --tests build/s12/lightrecon_uncertainty_test.exe --population build/s12/lightrecon_uncertainty_population.exe --out build/s12-witness
ctest --test-dir build/s12 -R lightrecon_uncertainty --output-on-failure
```

`--out` must be a new directory; it never overwrites an older witness. Each process
has a 600-second safety timeout (override `--timeout`), while deterministic operator
work budgets govern numerical results. Set the compiler runtime DLL path before
launching Windows GCC executables. The checked witness uses the existing
`build/s06-gcc` Release build with GCC 16.2.0 and Python 3.12.14.

Outputs:

- `tests-0/1.json`: joint uncertainty, alternatives, cancellation/negative controls.
- `population-0/1.json`: 48 actual C++ room fits and strict predictions, disjoint
  calibration/evaluation families, private evaluator outcome labels.
- `numerical.json`: deterministic dashboard, code/executable hashes and limitations.
- `resources.json`: actual process peak RSS and elapsed time, separate from numerics.
- `reliability.svg`, `risk-coverage.csv`: confidence and abstention diagnostics.

These are test/evaluator outputs containing ground-truth event labels. They are
**not solver input**. `Analyze` accepts training data plus aggregate validation
only; final evaluation colours and answers stay in the trusted controller.

The existence event uses injective position matching at radius `.16 * scene scale`.
Type calibration only has matched point cases; unknown type labels are excluded
explicitly. Exact-use labels are false because this population has no absolute
physical calibration. Field usefulness requires strict held-out improvement over
black and a meaningful leave-one-source-out prediction difference; it does not
require correct physical XYZ. Candidate ranks come from validation removal regret.
The frozen calibration bins are `[0,.02,.2,.6,1]`, minimum four independent families.
Their broad family Hoeffding bounds are reported; no stock-map probability is claimed.
Calibration uses the same positive-training/validation admission as `Analyze` (29
of 32 calibration draws); held-out RGB never determines that admission. Final
evaluation retains dark/abstained scenes in the coverage census. The preserved
`admission-diagnostic.json` records the superseded admission-mismatch experiment.

The dashboard includes all false candidates, missed source truth and abstention.
A lower training RGB error, a zero relative amplitude, an empty model or successful
serialization does not mean physical emitter recovery. Source positions are supplied
priors in the population experiment; discovery and general XYZ coverage are not tested.

Checked result: 81 C++ assertions, 48 procedural scenes, 32/16 split, two identical
runs. Of 17 final true sources, six lack a matching supplied candidate. Brier .21460
and ECE .14181 over 40 admitted records show limited calibration; a .9 point-rate
cutoff still accepts one false source. No tested source qualifies for exact use.
Peak process RSS is below 5.8 MiB on these compact tests; large-map inference and
stock confidence calibration remain unverified. See `results/validation.json` for
the 28/28 GCC, 3/3 targeted MSVC, SP/MP build and Python regression commands/results.
