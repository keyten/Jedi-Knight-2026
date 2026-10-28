# S02: CPU sun and sky hypotheses

`lightrecon/sun_fit.h` is the production CPU API. `Fit` sees training RGB only;
`ValidateAndPublish` admits frozen models using a separate spatial validation
fold; `Assess` only scores an already frozen model on untouched held-out data.
The result owns canonical **candidate** sun/environment assets and optional
sky17 payloads. No renderer sun, cvars, GPU state or map-load hook is changed.

See [method, scope and measured results](../../../docs/rend2-sun-sky-reconstruction-s02.md).
The repeated S03 task uses this same estimator; see its
[requirement audit and added captured-input tests](../../../docs/rend2-sun-sky-reconstruction-s03.md).
No Python light predictor is used for optimization. The harness reuses the
original BSP parser/UV mapping, existing spatial partitions, original RGB8
bytes and production C++ forward kernels. Python dependencies: standard library
only. Own-content authoring fixtures and tools: GPL-2.0-or-later.

From the repository root in PowerShell, with CMake/compiler on PATH:

```powershell
cmake -S tools/lightrecon_test -B build/s02-tools -G "Visual Studio 16 2019" -A x64
cmake --build build/s02-tools --config Release --parallel 6
ctest --test-dir build/s02-tools -C Release --output-on-failure
$tool = 'build/s02-tools/Release/lightrecon_bake_forward_tool.exe'
$q3 = 'build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe'
python -X utf8 tools/rend2/sun_fit/run.py --tool $tool --compiler $q3 --output build/s02-bakes-new
python -X utf8 tools/rend2/sun_fit/run.py --tool $tool --compiler $q3 --reuse build/s02-bakes-new --output build/s02-repeat-new
build/s02-tools/Release/lightrecon_sun_test.exe build/s02-cpu-new.json
python -X utf8 -m unittest discover -s tools/rend2/sun_fit -p 'test_*.py' -v
python -X utf8 tools/rend2/sun_fit/report.py --run build/s02-bakes-new --repeat build/s02-repeat-new --cpu build/s02-cpu-new.json --output build/s02-report-new
```

For single-configuration generators omit `Release/`; Unix omits `.exe`.
If q3map2 is absent, CPU analytic/BVH tests still work, but compiled-map tests
are not passed. The [existing pinned-compiler instructions](../../../docs/rend2-q3map2-validation.md)
give its source/version/hash. `run.py` rejects a different executable hash and
refuses to overwrite output directories. `--reuse` verifies immutable BSP and
recipe compiler identities, without invoking the compiler again. Complete BSP
byte repeatability across fresh compiler invocations is not assumed.

The suite has three sun positives (different receiver normals, wall shadow,
and a known fixed local lamp), a positive skylight v2, the original insufficient
skylight v1, fixed ambient, dark indoor and two conflicting coloured sun sources.
The v1 failed-positive record and shadow partial-coverage result remain explicit;
`Passed` is not printed for them. Acceptance never uses observed black held-out
data as a positive-light success.

The trusted controller owns `.map`, shader and `private-recipe.json` answers.
The C++ `--solar geometry.request train.txt validation.txt [heldout.txt]` adapter
is deliberately a **controlled-fixture adapter**: `JKSOLAR1` attests the selected
source encoding/recipe. It does not autodetect the historical profile of an
arbitrary stock BSP. Its geometry request contains no sun amplitude or target
direction unless `--authored-prior` explicitly supplies a zero-amplitude authored
direction prior. Validation RGB is opened only after `Fit` freezes parameters;
held-out RGB is opened only after validation selection, for value-only scoring.
It is a trusted staged harness, not an OS sandbox for malicious native solvers.
External optimizers must use the existing physically separated TrainingView
and restricted input mounts.

`results/validation.json` stores source/partition hashes, quantitative results,
per-model acceptance/rejection, supported receiver counts and retained failures.
Individual receiver diagnostics stay under ignored `build/`; the compact witness
contains summaries. `measurements.json` separately records actual peak process
working sets and timings. Unknown optical data is never converted to numerical
black or silently counted as complete coverage.
