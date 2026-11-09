# ASR02 IDF proposal experiment

[Research, results and recommendation](../../../docs/rend2-idf-localization-asr02.md).
This is an offline proposal experiment, not a replacement solver. It emits no
canonical asset and never enables a renderer consumer.

Run in PowerShell from the repository root. Python standard library only:

```powershell
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$tc='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin'
$env:PATH=$tc+';'+$env:PATH
& "$tc/cmake.exe" --build build/s06-gcc --parallel 2 --target lightrecon_hidden_tool lightrecon_hidden_test
New-Item -ItemType Directory -Path build/asr02-tools -Force | Out-Null
& "$tc/g++.exe" -std=c++11 -O2 -I shared/rd-rend2 tools/rend2/idf/geometry.cpp build/s06-gcc/core/librend2_lightrecon_core.a -o build/asr02-tools/idf_geometry.exe
& $py -X utf8 -m unittest tools.rend2.idf.test_idf -v
& $py -X utf8 tools/rend2/idf/run.py freeze --work build/asr02-new
& $py -X utf8 tools/rend2/idf/run.py evaluate --work build/asr02-new --out build/asr02-new-report
& $py -X utf8 tools/rend2/idf/report.py --work build/asr02-new --report build/asr02-new-report
```

Actual final execution: work `build/asr02-v3`, report `results/v3` in this folder.
Use fresh directories; neither freeze nor evaluate overwrites an experiment.
All report artifacts use UTF-8/LF with hashes that survive Git checkout.

Inputs are the existing owned compiler captures under
`build/x02/compiled-fixtures-frozen`, answer recipes under
`build/q3map2-bake-verified/answers`, X05's frozen sky-aperture control under
`build/x05/frozen-final`, and the stock hoth3 TrainingView used by ASR01.
`--fixtures`, `--answers`, `--stock`, `--geometry-tool`, and `--hidden-tool`
override those defaults. The original JA files remain local. Missing inputs are
not a successful dark prediction; stock truth remains unavailable even when its
observations are mounted. The six owned direct/sky compiler cases need their
preserved captures for byte-identical replay; see the existing
[compiled dataset preparation](../datasets/test_quality_pipeline.py) and
[q3map2 fixture workflow](../bake_model/README.md) to generate a new experiment.

The downloaded author PDFs are reused under `build/light-localization-research`;
their official URLs/checksums are in the earlier
[source inventory](../light_localization_research/sources.json) and final protocol.
They are read-only research inputs and are not redistributed.

`freeze` produces new owned-fixture folds by the existing geometry-only splitter
with one fixed seed/origin. It preserves all older ASR01 folds. Hidden sees the
same selected training rows, source volume and 384-node cap as the IDF variants.
The geometry helper accepts sanitized archives and geometry-only receiver files;
it never opens RGB, entities with source answers, validation or held-out targets.
Source answers open in the trusted controller only after **all** case/method
outputs freeze. This describes the data/API boundary, not an OS sandbox claim.

`raw` ignores known blocked rays only as a deliberately nonphysical occlusion
ablation; unknown rays are excluded in **every** mode. `geometry` uses known
visible paths; `censored` adds code intervals and uncertain-limiter weighting.
All modes require known-free, clear candidate centres. Their RGB8 quantities are
code proxies, not physical intensity bounds.

The passive reflection control is analytically authored on the compiled room's
geometry. It is not a q3map2 bounce replay or an arbitrary residual fitted to RGB.
Its fixed passive ceiling patch receives an externally prescribed illumination;
it contributes no local direct point roots. Sky-aperture observations are an
explicit planar subset, not a complete all-pages capture.

`report.py` verifies hashes, budgets, known-free candidate centres, exact repeats,
mathematical counterexamples and unchanged production sources, and writes tables.
All actual native Hidden commands/resources are retained in results. Native IDF
query counts, ray states, traversal work and per-cell outputs remain under work;
only aggregate diagnostics and owned candidate positions enter Git.

Policies v1/v2 and their failed experiment reports are retained to explain the
receiver-AABB crop, a dark/rank-deficient original spotlight fold, and the raw
ablation's unsafe unknown-ray treatment. They are historical failed configurations,
not admissible modes of the current prototype. v3 preserves v2's geometric volume,
new folds and all numerical gates while excluding unknown rays in all variants.
Only v3 is the final integration comparison; do not call the current executable a
byte-identical replay of older working-tree source hashes.
