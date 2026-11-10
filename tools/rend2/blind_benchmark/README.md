# ASR23 blind multi-method benchmark

This is an offline experiment, not a canonical asset producer. See the
[report](../../../docs/rend2-blind-benchmark-asr23.md),
[fixed numerical policy](policy-v1.json) and `results/v1` for the checked witness.
Python needs NumPy; all transport/visibility/source-support evaluation uses the
existing C++ core through a new geometry-only tool. Nothing is linked into a
renderer, and the historical solver is unchanged.

From the repository root in PowerShell:

```powershell
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc --target lightrecon_blind_basis_tool lightrecon_adaptive_tool lightrecon_regions_inspect lightrecon_dataset_quality lightrecon_grid_geometry_tool -j 2
& $py -X utf8 -m unittest tools.rend2.blind_benchmark.test_protocol -v
& $py -X utf8 -m tools.rend2.blind_benchmark.freeze --work build/asr23-new-frozen
& $py -X utf8 -m tools.rend2.blind_benchmark.run replay --work build/asr23-new-frozen --out build/asr23-new-a
& $py -X utf8 -m tools.rend2.blind_benchmark.run replay --work build/asr23-new-frozen --out build/asr23-new-b
& $py -X utf8 -m tools.rend2.blind_benchmark.run verify --work build/asr23-new-frozen --runs build/asr23-new-a --second build/asr23-new-b --out build/asr23-new-repeat.json
& $py -X utf8 -m tools.rend2.blind_benchmark.run evaluate --work build/asr23-new-frozen --runs build/asr23-new-a --out build/asr23-new-report
& $py -X utf8 -m tools.rend2.blind_benchmark.audit --work build/asr23-new-frozen --first build/asr23-new-a --second build/asr23-new-b --output build/asr23-new-audit
& $py -X utf8 -m tools.rend2.blind_benchmark.publish --work build/asr23-new-frozen --first build/asr23-new-a --second build/asr23-new-b --report build/asr23-new-report --audit build/asr23-new-audit --output build/asr23-new-publication
```

All output directories must be new. `freeze --help` exposes paths to the pinned
owned BSPs/captures, JA compiler, native binaries and preserved V02 bridge.
Missing required scenes, mismatching hashes or missing folds stop the experiment.
A fresh bake is a new experiment: q3map2 can change ancillary BSP bytes. Exact
replay uses the same frozen inputs, not a rebake described as identical.

The controller compiles two additional owned scenes (empty and mixed sun/local),
copies answer bytes into `private/`, publishes `public/<scene>/`, then writes
`protocol.json` before invoking inference. Twelve other owned scenes reuse pinned
original compiler BSPs; both original V02 bridge variants retain their partitions.
The branch, revision, actual Git remote, source/binary/input/answer hashes and
partition counts are recorded. The local remote is currently `keyten/BetterJK`,
not the requested repository spelling; the active development branch is correct.

The checked final experiment uses `build/asr23-frozen-v5`, `build/asr23-final-a`,
`build/asr23-final-b` and `build/asr23-report-final`. The prior experiment's
results and source snapshot are retained under `results/initial`; adapter defects
were corrected after inspecting its held-out report. Seeds, costs and numerical
acceptance gates were unchanged. See the report for the resulting limitation.

Only the public scene directory, policy and native-tool directory are passed to
the solver subprocess. The solver never imports the evaluator, opens source
answers or receives validation/held-out targets. Public receiver geometry across
roles is allowed. This is an auditable process/API boundary, **not an OS sandbox**
against a malicious optimizer with arbitrary filesystem access.

The solver freezes a training-ranked model path for every method. The trusted
evaluator selects from those paths with validation, writes and hashes all
selections, then scores held-out RGB and opens source answers. No held-out value
can refit, propose, choose a method, tune a threshold or change a source. Code,
binary, public/evaluator and answer hashes are checked around each replay.

Grid inputs are actual original JA records and indirection, not generated probes.
Byte-identical records share one train/validation/held-out hash assignment; only
training records reach inference. RGB codes group similar directions; grid
brightness is never regressed against lightmap brightness. Lightmap held-out
scores are conditional on the allowed original-grid evidence; this does **not**
claim a completely unseen spatial volume across every modality.

The nine methods are strict JA Hidden proposals, fixture-only, grid-only,
adaptive lightmap-only, grid+lightmap, all cues, sun-off, area-off and
adaptive-off. `ja_baseline` means the **unchanged strict JA proposal method plus
the common benchmark fitter**. It is not a replay or claimed replacement of an
entire shipping JA inverse-lighting pipeline. This controlled comparison isolates
proposal/evidence changes from fitting and scoring changes.

All methods use the same modern native transfer evaluation, fixed forward RGB8
appearance hypothesis, training fitter, validation selector, source-count cost,
row schedule and source caps. No supplied source XYZ enters a request. Fixture
poses come from original emitting shader faces; grid poses come from bounded
direction intersections; Hidden sees only training RGB and captured geometry.
Shadow/occlusion evidence enters the native search responses and final transfer
columns. This suite does not isolate a shadow-estimator ablation.

Finite emitting prisms are evaluated by their downward original rectangle, with
the thin strip explicitly serving as a **tube approximation**. It is not a
compiled analytic-cylinder source or proof that a historical tube existed.
The original bridge has three local point/spot roots plus an emitting prism;
its positional denominator includes all four. Sun alternatives are a fixed
five-direction geometry-only catalog in scenes with captured sky. They receive
separate source counts and are excluded from local-source precision/recall.

The small-map RGB8 bakes are compared under one explicit same-material,
gamma=1/compensate=1/exposure=0 forward-appearance assumption. The new small
benchmark fitter fits native modern relative columns to quantization intervals;
it never inverts RGB8 into calibrated linear irradiance. It sums source RGB,
applies coupled peak compression, then scores. Relative powers have no SI
meaning. This is a conditional aesthetic model, not compiler identification.
Indirect-bounce scenes test misspecification of this direct model; no bounce is
silently promoted to an exact primary source or claimed recovered by the fit.

`lightrecon_blind_basis_tool` accepts `JKBASIS23` geometry-only candidate/receiver
rows or `JKPOINTS23` point classification. It reuses retained BSP domain checks,
`SourceSupport::Check`, `ShapeSelection::Rank`, `Shape::Prepare` and sky rays.
Unknown receiver responses exclude the affected alternative with explicit counts;
unknown geometry never becomes a zero response or certified free space.

The empty compiler scene collapses to solid black texels with degenerate UVs.
Its repeated geometry queries are correlated **training-only** evidence. Source
abstention is tested, while independent held-out illumination remains Unavailable.
No licensed stock geometry or textures are checked in. True stock source
positions and stock precision/recall remain Unavailable.
