# X05 forward acceptance

[Specification](../../../../docs/rend2-benchmark-acceptance-x05.md),
[frozen policy](policy.json), [witness](results/README.md).
Python standard library only; GPL-2.0-or-later. No solver or new dependency.

Run from the repository root. The old 12/22 frozen fixture paths default to the
existing local captures described in the spec; override `--old12`, `--recipes12`,
`--old22` if needed. Missing data fails, rather than silently skipping a stage.
Use fresh output/compiler directories:

```powershell
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$q3='build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe'
& $py -X utf8 tools/rend2/evaluation/x05/run.py --output build/x05-new --compiled build/x05-compiled-new --compiler $q3 --inspector build/lightrecon-core-renderers/Release/lightrecon_regions_inspect.exe --quality-tool build/lightrecon-core-renderers/Release/lightrecon_dataset_quality.exe
& $py -X utf8 -m unittest discover -s tools/rend2/evaluation/x05 -p 'test_*.py' -v
```

For integration assertions, set `$env:X05_RUN='build/x05-new'`. The tests skip
those assertions when a complete local controller run is absent. Core numerical
and blind-boundary tests remain self-contained. Real archive sanitization is
performed by the existing CPU tools; tests' placeholder geometry is not proof of
binary archive sanitization.

To measure actual Python process peak working set, prepend the existing wrapper:

```powershell
& $py -X utf8 tools/rend2/datasets/measure.py build/x05-new-memory.json $py -X utf8 tools/rend2/evaluation/x05/run.py --output build/x05-new --compiled build/x05-compiled-new --compiler $q3 --inspector build/lightrecon-core-renderers/Release/lightrecon_regions_inspect.exe --quality-tool build/lightrecon-core-renderers/Release/lightrecon_dataset_quality.exe
```

This memory measurement excludes child processes. Full run output contains the
audit, private targets, blind packages, OBJ/TSV geometry diagnostics, 35 boundary
records, result and manifest. Never mount this entire tree into a solver.

Re-run without `--compiler`, with the **same** `--compiled` directory and optional
`--audit FIRST_RUN/audit.json`, to test deterministic analysis. Recipe/source/BSP
hashes are checked. Preserve each full-BSP identity even if recompilation changes
only lightgrid or brush-side metadata; do not overwrite old witnesses.

```powershell
& $py -X utf8 tools/rend2/bake_model/compare_bakes.py --q3map2 $q3 --output build/x05-kernels-new
& $py -X utf8 tools/rend2/evaluation/x05/verify_reproduction.py --original build/bake-model-comparison-final/authoring/base/maps --rebuilt build/x05-kernels-new/authoring/base/maps --output build/x05-recompile.json
& $py -X utf8 tools/rend2/evaluation/x05/report.py --run build/x05-new --measurement build/x05-new-memory.json --output build/x05-compact-new
```

For external frozen **forward** predictions, use the trusted controller only:

```json
{"schema":"org.openjk.forward-predictions.v1","manifest_sha256":"<hash>",
 "cases":{"<fixture-id>":[{"id":"<requested-observation-id>",
 "status":"Available","rgb":[12.5,12.5,12.5],"domain":"EncodedRGB8",
 "primaries":"EncodedSourceRGB",
 "comparison_id":"NetRadiant-d9338309e-ColorToBytes-gamma1-compensate1-exposure0-v1"}]}}
```

Every `required:true` fixture in the manifest must be present, including entries
whose role is `diagnostic`. Only the two `required:false` doorway diagnostics may
be omitted. Unavailable/unknown
records contain a status and reason, **no RGB**. The physical-photometry request
uses `NotComparable`. No packet may
override truth, policy or metadata. Sources intentionally given to a known-source
forward oracle must never be delivered to an inverse-recovery solver.

```powershell
& $py -X utf8 tools/rend2/evaluation/x05/score.py --manifest build/x05-new/manifest.json --predictions private/frozen-forward.json --output build/x05-external-score.json
```

Exit 1 is a failed forward contract; an integrity/schema error is an exception.
This command scores the prediction/status protocol. Profile selection and motion-
derivative diagnostics must also pass `run.py`; neither command measures source
recovery. The manifest checked into `results/` is an actual local witness with
private paths, not a portable packet of hidden answers; regenerate it locally.
