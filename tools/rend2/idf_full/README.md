# Full IDF discovery/refinement follow-up

[Results and method correspondence](../../../docs/rend2-idf-full-followup.md).
This implements the full research discovery/refinement cycle for diffuse isotropic
points. It does not replace the historical solver or publish a canonical asset.

Run from the repository root in PowerShell:

```powershell
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
# NumPy 2.3.5 is supplied by this Python runtime. SciPy is workspace-local.
& $py -m pip install --target build/idf-full-deps --no-deps scipy==1.16.3
& $py -X utf8 -m unittest tools.rend2.idf_full.test_algorithm tools.rend2.idf.test_idf -v
& $py -X utf8 tools/rend2/idf_full/run.py freeze --work build/idf-full-new
& $py -X utf8 tools/rend2/idf_full/run.py evaluate --work build/idf-full-new --out build/idf-full-new-report
& $py -X utf8 tools/rend2/idf_full/report.py --work build/idf-full-new --report build/idf-full-new-report
```

Actual final run: `build/idf-full-v3` -> `results/v3`, policy-v2. The preceding
completed coarse experiment is `build/idf-full-v2` -> `results/v2`, policy-v1.
Old numerical source files and policy are retained under `results/v2/source-snapshot`
with their original hashes. Restore that snapshot into an isolated checkout to
replay its exact source; the current default runs the finer experiment. A first
development run stopped at a negative relocation residual before complete export;
that crash is not a scientific result. The corrected guard rejects an invalid
move without manufacturing a zero residual.

The native geometry adapter is the previously built
`build/asr02-tools/idf_geometry.exe`, unchanged from ASR02. To rebuild it, use
[the existing command](../idf/README.md). Original owned compiler inputs and
immutable train/final views are resolved from ASR02's v3 protocol. Author PDFs
remain at `build/light-localization-research`; their content hashes are frozen.
No stock JA original assets are copied into these reports.

Directories must be fresh; old runs are not overwritten. `freeze` authors ideal
linear fixtures, copies compiler training observations into an explicit conditional
preencoding-midpoint hypothesis, seals answers/evaluation hashes, and precomputes
geometry-only coefficients for the fixed room lattice and spherical probes.
This cache uses neither source truth nor observation RGB. Its 14,749 source-position
queries per case are charged separately from subsequent optimizer/refinement work.
The numerical code receives only training geometry, observations, bounds and policy.
This is an API/data separation, not an OS sandbox claim.

`evaluate` compares proposal-only, initial NNLS+LM fitting, and the full repeated
cycle. Every case/variant repeats twice. All fitted models freeze before the
controller reads any answer or evaluation RGB. Post-fit evaluation never changes
parameters, thresholds or source selection. Ideal controls use an explicit tiled
geometry split; compiler cases reuse ASR02's immutable folds. The two experiments
are sequential development comparisons on the same data, not independent held-out
trials or historical certification.

The finer policy adds two room-local 3x3x3 peak refinements and reduces normalized
intensity increments from .025 to .005. No fixture, fold, source count answer,
matching tolerance or acceptance threshold is adjusted. The 150,000-position
limit is a **soft local-refinement** cap; all further optimizer queries are counted.
It is not a renderer limit or a promise that every query type stops at that cap.

For compiler observations, saturated vectors are excluded before selection/fitting.
The remaining gamma-1 floor-code intervals are represented by their midpoints in
a declared compiler-relative hypothesis. This does not establish calibrated
irradiance or an exact compiler forward model. Residual subtraction is never
performed on arbitrary stock encoded RGB8.

Numerical results include partial and negative-residual failures. Tiny optimized
slots remain in raw models; evaluation counts sources above the predeclared 1%
relative intensity threshold. A low training error is not a source certificate.
The uniform indirect-fill control intentionally violates the finite-point model.
Its fitted lamps are false explanations, not accepted emitters.

`report.py` verifies source/dependency artifacts, exact repetitions and complete
stage coverage, runs 11 numerical/regression tests, and creates the result tables.
Compiler mismatch and control failures remain explicit. The discovery code and
all production modules are unchanged by report generation.
