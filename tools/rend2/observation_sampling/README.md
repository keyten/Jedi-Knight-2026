# X10 hierarchical observation sampling

This is an observation-admission and candidate-coverage benchmark, **not source
recovery**. Every candidate prediction uses the existing production CPU hidden
provider/forward evaluator. There is no new Python lighting formula or runtime
consumer. Standard-library Python only; C++11, no GL/game launch required.

The CPU API is `lightrecon/sampling.{h,cpp}`:

- `Build(map, frozenPartition, geometry, policy, read, plan)` accepts geometry and
  immutable roles only. It freezes geometry for validation/final-held-out.
- `Refine(plan, trainingSignals, read, refined)` accepts only whitelisted training
  IDs. It cannot accept hidden RGB. Plan/role tampering invalidates the keys.
  An optional final `priorities` argument accepts bounded training-residual,
  candidate-influence and supported-shadow selection ranks for those same IDs.
  These ranks do not change statistical weights or source confidence.
- Separate selections are returned for coarse coverage, discovery, source
  position, possible boundary refinement, model validation and final held-out.
  Census stays an exhaustive streaming count, not a sample selection.
- Correlation bins are a conservative geometric proxy, not Fisher information.
  Actual receiver area, represented area and normalized statistical weight are
  separate. Transport support remains the independent X09 mesh.

Build and run controlled checks:

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build/s06-gcc -j 2
New-Item -ItemType Directory -Force build/x10 | Out-Null
build/s06-gcc/lightrecon_sampling_test.exe build/x10/controlled.json
build/s06-gcc/lightrecon_sampling_test.exe build/x10/controlled-repeat.json
ctest --test-dir build/s06-gcc --output-on-failure -j 2
python -X utf8 -m unittest discover -s tools/rend2/observation_sampling -p 'test_*.py' -v
python -X utf8 -m unittest discover -s tools/rend2/datasets -p 'test_blind_views.py' -v
python -X utf8 tools/rend2/observation_sampling/verify_results.py
```

For the seven existing private all-pages stock archives:

```powershell
$private="$env:TEMP/jk-lightrecon-all-stock-20261031"
python -X utf8 tools/rend2/observation_sampling/run.py --private $private --revision x10-final-e
python -X utf8 tools/rend2/observation_sampling/run.py --private $private --revision x10-final-f
```

Use new revision names if outputs exist. Each map is a bounded child (600 s,
1 GiB RSS); only the exact owned child can be terminated. Actual Windows peak RSS
and wall time are recorded outside deterministic witnesses. The tool retains the
existing 512-WU sparse baseline and makes a separate hierarchical selection.
Neither fold block size/origin nor footprint/filtering gaps are reduced. Local
evaluation may use neighbouring independent blocks rather than requiring all
folds inside one dense cell.

Native interface:

```text
lightrecon_map_solver archive.jkobs existing-output-directory BSP-SHA sampling-only
lightrecon_map_solver archive.jkobs existing-output-directory BSP-SHA hierarchical
```

`sampling-only` runs census, selection, frozen evaluation-signal diagnostics and
up to four old/new candidate comparisons. `hierarchical` passes the selected
observations and explicit correlation/statistical weights to the existing
producer. It does not turn unavailable solar, transport or exact-light support
into a successful reconstruction. This benchmark records sampling/provider work;
it does not claim an end-to-end stock solve.

Private outputs:

- `sampling.json`: old/new counts, within-material contrast, surface/normal/block
  support, exact scale choices, admission reasons, per-purpose budgets, frozen
  evaluation signal, deferred neighbourhoods, data-read/work/memory diagnostics.
- `sampling-proposals.json`: same-neighbourhood old/new production provider
  calls, failures, proposal counts and actual trace work; no stock XYZ truth.
- `hierarchical-geometry.tsv`: stable IDs, folds, correlation groups, positions,
  actual receiver area, represented area **not physical transport area**, and
  dimensionless statistical weight. No RGB targets. Licensed geometry stays private.
- Existing `census.json` and old `selected-geometry.tsv` preserve the old protocol.

Known compiler scenes reuse the pinned compiled fixtures and their **existing**
blind partition manifest; no rebake and no seed/gap tuning:

```powershell
python -X utf8 tools/rend2/observation_sampling/compiled.py --output build/x10/compiled-final-e
python -X utf8 tools/rend2/observation_sampling/compiled.py --output build/x10/compiled-final-f
python -X utf8 tools/rend2/observation_sampling/collect.py --private $private --revision x10-final-e --repeat-revision x10-final-f --compiled build/x10/compiled-final-e/results.json --compiled-repeat build/x10/compiled-final-f/results.json --controlled build/x10/controlled.json --repeat build/x10/controlled-repeat.json
```

An opt-in `prepare_blind.py --sampling-geometry` creates
`inputs/receivers-geometry.jsonl`: geometry/roles from every partition, with an
exact allowlist and no target colours/answers. `TrainingView.receiver_geometry()`
validates this separate member. Existing input directories remain compatible.
The native `JKSAMPLE1` request contains that geometry and **training RGB only**.
Answers and evaluator-owned RGB are loaded after native completion. Frozen
evaluation IDs are returned by the native tool so positive-signal checks use
the actual selected independent receivers.

Nine fixture scopes × 16/48 discovery rows are compared: point, two colours,
spot, doorway/shadow, thin wall, coloured bounce/materials, a predeclared
`bounce_colour` receiver half-space `x < -32`, bounce control and skylight.
The half-space has no local lamp in the private truth, but can still yield
source-like transport proposals. The original spotlight training fold has no
positive illumination; this remains Unavailable rather than being reseeded.
Recall is reported at the declared grid radius and at one quarter of it, with
nearest distances, unmatched alternatives and positive held-out support. None
of these counts is calibrated source-existence confidence or emitter-count recovery.

Public results contain aggregate/hash evidence only. Known q3map2 recipes are
not evidence of the original stock Raven compiler. Original stock transfer,
primaries, materials and source photometry remain explicitly uncertain.

`collect.py --checks path.json` can additionally freeze an independent CI/build
record. If omitted, checks are explicitly `Unavailable`, not inferred passed.
The supplied record does not replace running the commands above. The checked-in
record identifies exact test/build logs and initial failed attempts separately.
