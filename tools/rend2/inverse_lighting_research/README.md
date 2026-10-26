# Inverse-lighting mathematical research checks

See [the research design](../../../docs/rend2-inverse-lighting-research.md).
These are small deterministic analytical experiments, **not a reconstruction
solver**. They do not ingest maps, initialize lights, write canonical assets or
integrate with a renderer. Only Python's standard library is used.

Run from repository root with Python 3.10 or later:

```powershell
python -m unittest discover -s tools/rend2/inverse_lighting_research -p 'test_*.py' -v
python tools/rend2/inverse_lighting_research/math_checks.py --output build/inverse-lighting-research/math-report.json
python tools/rend2/inverse_lighting_research/math_checks.py --output build/inverse-lighting-research/math-report-repeat.json
# Same interpreter/platform, no time/host-path/random fields in the report:
Get-FileHash build/inverse-lighting-research/math-report*.json -Algorithm SHA256
```

`math_checks.py` contains closed-form examples for target mass lumping,
inverse-square world/metre conversion and position derivatives, saturation as
an interval constraint, one-column amplitude identifiability and original Q3
grid packing. The latter is a source-derived counterexample, **not the claimed
JA grid forward model**. Directions are receiver-to-source. The tests explicitly
show why scale, collocated sources, unconstrained indirect residuals and hard
shadow steps are ambiguous.

## Recorded run, 2026-10-26

- 25 Python tests passed.
- The linear single-triangle peak `(0,0,1)` becomes `(0.25,0.25,0.5)` under
  thesis Eq. 3.7 with degree-two exact quadrature; integral and constants survive.
- Maximum absolute difference between the smooth analytical position gradient
  and central differences (`h=1e-5`) was `8.36067326481782e-12` at the recorded
  test position. This excludes hard visibility transitions by construction.
- A 1 cd light at 1 m and a 4 cd light at 2 m give the identical single normal-
  incidence sample. This is an ambiguity demonstration, not source recovery.
- Predictions 1 and 4 both satisfy the chosen saturated RGB8 lower bound;
  treating 255 as an exact target would impose unsupported information.
- Original Q3 pre-byte packing maps a direct-only RGB contribution `(4,2,1)`
  to directed `(4,2,1)` **and ambient `(1,.5,.25)`**, with no indirect source.
- Repeated report generation was byte-identical on the recorded interpreter;
  floating-point last bits may differ on other platforms, so tests use tolerances.
  Recorded report SHA256:
  `0a012a9608db1d09eccf2b7c42d8741f0d99bce3a039c55059df08c71bb433bb`.

Existing CPU contracts were also freshly built and run:

```powershell
cmake --build build/lightrecon-disk-standalone --parallel 4
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
```

All 16 CTest targets passed (13.22 seconds recorded total). The pre-existing
GCC configuration includes schema/API/core/controls/disk/lifecycle, image/grid/
decode/surface/classification, visibility/regions/evidence and offline tests.
For a clean standalone configuration use:

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-research-cpu
cmake --build build/lightrecon-research-cpu --config Release --parallel 4
ctest --test-dir build/lightrecon-research-cpu -C Release --output-on-failure
```

The local run used Python at
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`,
and prepended `../OpenJK/build/toolchain/w64devkit/bin` to process PATH for CMake/GCC.
No C++ code, shader, CMake list or runtime behaviour was changed. No new SP/MP
build, q3map bake, optimization benchmark or visual gameplay test was performed.
Previously executed compiler/runtime witnesses remain in
[`rend2-q3map2-validation.md`](../../../docs/rend2-q3map2-validation.md).

## Reference verification

`sources.json` pins the thesis PDF hash, page numbering, versioned 2024 framework
paper and primary compiler source snapshots. Downloaded reference material and
all 67 rendered/OCRed thesis pages were kept under ignored
`build/inverse-lighting-research/`, outside the source distribution.

To reproduce the visual reference check, download the PDF from the manifest,
verify its SHA256, and use Poppler (or a PDF reader):

```powershell
Get-FileHash build/inverse-lighting-research/schwengerer-thesis.pdf -Algorithm SHA256
pdftoppm -r 140 -png build/inverse-lighting-research/schwengerer-thesis.pdf build/inverse-lighting-research/page
```

Printed page `n` is image/PDF page `n+16`. Inspect PDF pages 30–31 for Eqs.
3.1–3.7, page 40 for Algorithm 4.1, page 47 for Table 5.1, and pages 49–50 for
the close-up source-placement failures. Copy/pasted text and OCR alone cannot
reliably reproduce the equations because the original PDF's character mapping
is broken. Source-derived claims have printed page/equation locators in the
research document; J1–J10 and the proposed test thresholds are our design.
