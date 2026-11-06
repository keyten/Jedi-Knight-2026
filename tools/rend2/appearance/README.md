# X18 native legacy-appearance field benchmark

This reuses P01/P02/P05/P06/V01; Python only freezes inputs and summarizes native
receipts. It does not implement lighting equations or promote lightmap samples
into physical emitters. See [the technical report](../../../docs/rend2-legacy-appearance-x18.md).

Build `tools/lightrecon_test` in Release with the existing CMake infrastructure.
Use fresh output directories and the original private all-stock capture:

```powershell
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $py -X utf8 tools/rend2/appearance/run.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out build/x18-stock-v3
& $py -X utf8 tools/rend2/appearance/report.py --run build/x18-stock-v3 --out build/x18-public-summary
& $py -X utf8 -m unittest tools.rend2.appearance.test_report
```

The protocol is written before evaluating the native executable. Selection uses
original geometry/stable IDs only. Training and admission validation are passed
to `Fields::Build`; final RGB is used after coefficients/admission freeze.
Missing predictions retain their full denominator and null error. Tables compare
methods on common supported rows and also preserve baseline full-fold coverage.

Private `.jklr` data stay beneath the ignored run output's per-ROI cache folders.
Published results contain hashes and numerical receipts, not stock meshes or
prepared-field positions. Original stock lightgrid locations/RGB are immutable.
The current result is an encoded scalar RGB appearance proxy, not lux/nits,
incoming radiance, direct/indirect separation or original lamp reconstruction.
