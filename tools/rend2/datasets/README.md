# Offline lighting datasets

Run from repository root:

```sh
cmake -S tools/lightrecon_test -B build/lightrecon-offline
cmake --build build/lightrecon-offline --config Release --parallel 4
ctest --test-dir build/lightrecon-offline -C Release --output-on-failure
python tools/rend2/datasets/run_pipeline.py --tool build/lightrecon-offline/Release/lightrecon_dataset_tool.exe --output build/offline-suite
```

Single-configuration generators omit `Release/`; Unix omits `.exe`. No Python
dependencies, GL, game or compiler for BSPs is required. Use a fresh output folder.
Only mount `inputs/` for a blind solver; keep answers, authoring maps and bake
recipes private to the evaluator. Full archives contain all sample partitions:
the evaluator must withhold held-out RGB, not merely hide the answer file.

The [format, export command, units, ownership, synthetic scenes, optional q3map2
recipe and solver guide](../../../docs/rend2-offline-observation-datasets.md)
document the complete workflow. `test_runtime_export.py` exercises licensed local
stock SP map loading and repeated CPU exports. No game content belongs in Git.

`validation.json` is a small validation witness, not an observation dataset or
ground-truth answer. Generated own-content fixtures are GPL-2.0-or-later;
repository code is governed by `LICENSE.txt`. q3map2 bakes are explicitly
unverified when no compiler is supplied.

The subsequent [real q3map2/SP validation](../../../docs/rend2-q3map2-validation.md)
adds `test_q3map2_runtime.py` (actual map load, raw CPU audits and blind admission),
`verify_q3map2.py` (independent BSP mapping, lighting and visibility checks) and
`test_q3map2_recipe.py` (six dependency-free reporting guards). The small
`q3map2-validation.json` witness records that run. Compiler outputs and all game
content remain local under ignored `build/` directories. Full BSP reproducibility
and identical-input dataset reproducibility are reported separately.
