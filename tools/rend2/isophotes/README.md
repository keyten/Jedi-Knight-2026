# ASR04 planar source cues

[Technical report, derivation and limitations](../../../docs/rend2-isophote-cues-asr04.md).
Research-only scalar-field estimators; no canonical asset or renderer output.

From the repository root, Python 3.8+ standard library:

```powershell
python -X utf8 -m unittest tools.rend2.isophotes.test_isophotes -v
python -X utf8 tools/rend2/isophotes/run.py --out build/asr04-new
python -X utf8 tools/rend2/isophotes/run.py --out build/asr04-new-repeat
python -X utf8 -m tools.rend2.isophotes.verify build/asr04-new --repeat build/asr04-new-repeat
```

The unit tests need no assets. The numerical study uses the existing owned
q3map2 JA captures in `build/x02/compiled-fixtures-frozen`, original BSPs in
`build/q3map2-bake-verified/authoring/base/maps`, and recipes in
`build/q3map2-bake-verified/answers`. Override with `--fixtures`, `--bsp`,
`--answers`. Missing inputs or digest mismatches fail explicitly. Output
directories must be fresh. Existing capture preparation is documented in the
[bake workflow](../bake_model/README.md) and
[dataset controller](../datasets/test_quality_pipeline.py).

The estimator receives only selected planar observations and metadata; recipes
open in the trusted scoring controller after estimates freeze. New interleaved
within-patch holdouts do not modify earlier benchmark folds. They are correlated
through the bake and do not establish independently validated source accuracy.

`results/v2` contains the measured 20-case comparison. `C` means conditional
feature availability, never primary-source certification; `U` means unavailable.
Even admitted model heights can be wrong under gamma or transport. All methods
explicitly keep physical depth/type/orientation unavailable. The RGB8 scalar is
a code proxy, not calibrated irradiance. No ray query is assumed clear.
