# ASR01 aesthetic source-discovery baseline

See the [measured report and acceptance definitions](../../../docs/rend2-aesthetic-source-baseline-asr01.md),
[frozen policy](policy-v1.json), and [results](results/v1/results.json).
This controller calls existing production C++; it contains no lighting solver.
Python standard library only. No game or renderer consumer is launched.

Run from the repository root in PowerShell. These are the actual local tool paths:

```powershell
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$tc='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin'
$env:PATH=$tc+';'+$env:PATH
& "$tc/cmake.exe" --build build/s06-gcc --parallel 2 --target lightrecon_hidden_tool lightrecon_map_solver lightrecon_hidden_test lightrecon_sun_test lightrecon_joint_test lightrecon_producer_test
& $py -X utf8 -m unittest tools.rend2.aesthetic_baseline.test_acceptance -v
& $py -X utf8 tools/rend2/aesthetic_baseline/run.py freeze --work build/asr01-new
& $py -X utf8 tools/rend2/aesthetic_baseline/run.py evaluate --work build/asr01-new --out build/asr01-new-report
```

The committed execution used `--work build/asr01-v2` and
`--out tools/rend2/aesthetic_baseline/results/v1`. Both phases refuse existing
output directories. Do not overwrite the committed results for a repeat.
The initial path-lookup attempt is retained locally at
`build/asr01-initial-path-report`: it found the stock ROI but missed the all-pages
archive under the capture manifest. The corrected controller follows that manifest
and verifies the same X12 hashes. No threshold, fold, candidate policy or solver
changed in that correction. A separate confirming stock execution is retained at
`build/asr01-stock-report-v1`.

Default owned inputs are the previously compiled and captured
`build/x02/compiled-fixtures-frozen/{point_room,two_colour,skylight}-views/`;
answers/BSPs are under `build/q3map2-bake-verified/`. The stock ROI uses
`build/x02/hoth3-views/inputs`. The original stock all-pages archive is resolved
under `$env:TEMP/jk-lightrecon-all-stock-20261031`, including its `captures.json`
fallback. Override these locations with `--fixtures`, `--answers`, `--stock-roi`,
`--private` and `--native`. Paths are machine-specific; content hashes are the
identities. No licensed stock data are copied into Git.

`freeze` copies executables, verifies TrainingView and partition hashes, creates
training-only requests, and records source IDs, selected sample IDs, full fold
hashes/counts and source-code hashes before running a native child. It hashes
answer files without opening their source parameters. `evaluate` verifies all
dependencies, runs each request twice, then opens owned source answers for distance
scoring. Native Hidden receives only sanitized geometry and the frozen training
request. It never receives answer paths or evaluation RGB. This is a controlled
protocol boundary, not a claim of an OS-isolated filesystem sandbox.

Every native command, exit code, elapsed time and observed peak RSS is in
`results.json`. Native stdout, private requests, stock debug geometry and caches
remain under `--work`. JSON hash differences due solely to native timing fields
are excluded from the separately recorded semantic comparison. Stocks have no
source-answer precision/recall and no fabricated final RGB error.

The original owned-map recipes, compiler identity and all input hashes are pinned
in `protocol.json`. Fixture regeneration uses the existing
[dataset workflow](../datasets/README.md),
[compiled capture checks](../datasets/test_quality_pipeline.py), and
[q3map2 fixture instructions](../bake_model/README.md). The preserved captures are
required for a byte-identical replay: a fresh compiler bake can differ in ancillary
BSP bytes, and must be frozen as a new experiment rather than called the same input.
Missing owned fixtures stop the run. Missing stock inputs produce an explicit
**Blocked** stock gate; they never count as dark scenes or successful stock fits.

The six policy tests exercise empty models, black predictions, unavailable stock,
unknown geometry, missing predictions, false emitters and wall leaks. They do not
replace or relax any native strict test or acceptance gate.
